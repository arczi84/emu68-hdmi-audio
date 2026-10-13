/* Shared by HDMITest and the AHI driver. One hardware owner at a time. */
/* SPDX-License-Identifier: GPL-3.0-or-later
 * HDMI initialization adapted from Circle hdmisoundbasedevice.cpp,
 * Copyright (C) 2021-2024 R. Stange <rsta2@o2online.de>.
 * AmigaOS/Emu68 test harness, 2026.
 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/tasks.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/semaphores.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/devicetree.h>
#include <proto/mailbox.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "tone.h"
#include "clock_reply.h"
#include "dma_plan.h"
#include "dma_control.h"
#include "stream.h"

APTR DeviceTreeBase, MailboxBase;
static ULONG io;

/* BCM2711 HDMI0 offsets relative to the translated 0x7e000000 bus range. */
enum {
    CTRL=0xf20010, THRESH=0xf20014, FORMAT=0xf20018, DATA=0xf2001c,
    RATE=0xf20020, MAP=0xf0079c, CONFIG=0xf007a0, PACKET=0xf007b8,
    RAMCFG=0xf007bc, RAMSTATUS=0xf007c4, CRP=0xf007c8,
    CTS0=0xf007cc, CTS1=0xf007d0, PHY=0xf00f04,
    RAMAUDIO=0xf01b90, TIMER=0x3004
};
#define AUDIO_BIT (1ul << 4)
#define PACKETS_ENABLED (1ul << 16)
#define FULL (1ul << 11)
#define ENABLE (1ul << 3)
#define IDLE_CONTROL ((1ul << 15) | 6)

static ULONG rd(ULONG offset)
{
    return __builtin_bswap32(*(volatile ULONG *)(io + offset));
}
static void wr(ULONG offset, ULONG value)
{
    *(volatile ULONG *)(io + offset) = __builtin_bswap32(value);
    /* Same MMIO sequencing convention as Emu68's native drivers. */
    __asm__ volatile ("nop" ::: "memory");
}

static const void *prop(APTR key, const char *name, ULONG *length)
{
    APTR p = DT_FindProperty(key, (CONST_STRPTR)name);
    if (!p) return NULL;
    if (length) *length = DT_GetPropLen(p);
    return DT_GetPropValue(p);
}
static ULONG cell(APTR key, const char *name, ULONG fallback)
{
    ULONG n = 0;
    const ULONG *p = prop(key, name, &n);
    return p && n == 4 ? *p : fallback;
}
static int compatible(APTR root)
{
    ULONG n = 0;
    const char *p = prop(root, "compatible", &n);
    while (p && n) {
        const char *end = memchr(p, 0, n);
        ULONG len;
        if (!end) return 0;
        if (!strcmp(p, "brcm,bcm2711")) return 1;
        len = (ULONG)(end - p) + 1;
        p += len;
        n -= len;
    }
    return 0;
}
static int discover(void)
{
    APTR root, soc, emu;
    ULONG ac, pc, sc, stride, length = 0, i;
    const ULONG *ranges;
    DeviceTreeBase = OpenResource((CONST_STRPTR)"devicetree.resource");
    if (!DeviceTreeBase) { puts("Missing devicetree.resource."); return 0; }
    root = DT_OpenKey((CONST_STRPTR)"/");
    soc = DT_OpenKey((CONST_STRPTR)"/soc");
    emu = DT_OpenKey((CONST_STRPTR)"/emu68");
    if (!root || !soc || !emu || !compatible(root)) {
        puts("Requires Emu68 on BCM2711 (Raspberry Pi 4/CM4). No MMIO accessed.");
        if (root) DT_CloseKey(root);
        if (soc) DT_CloseKey(soc);
        if (emu) DT_CloseKey(emu);
        return 0;
    }
    DT_CloseKey(emu);
    ac = cell(soc, "#address-cells", 1);
    pc = cell(root, "#address-cells", 2);
    sc = cell(soc, "#size-cells", 1);
    ranges = prop(soc, "ranges", &length);
    stride = ac + pc + sc;
    if (ranges && ac == 1 && (pc == 1 || pc == 2) && sc == 1 &&
        length % (stride * 4) == 0) {
        for (i = 0; i < length / 4; i += stride) {
            ULONG bus = ranges[i], cpu = ranges[i + ac + pc - 1];
            ULONG size = ranges[i + stride - 1];
            if (pc == 2 && ranges[i + ac] != 0) continue;
            if (bus == 0x7e000000ul && size >= 0x00f20024ul &&
                cpu >= 0xf0000000ul && cpu <= 0xff0dffdbul) {
                io = cpu;
                break;
            }
        }
    }
    DT_CloseKey(soc);
    DT_CloseKey(root);
    if (!io) puts("No supported Emu68 /soc peripheral mapping. No MMIO accessed.");
    return io != 0;
}

static int wait_audio(int enabled)
{
    ULONG start = rd(TIMER), spins;
    for (spins = 0; spins < 2000000; ++spins) {
        if (!!(rd(RAMSTATUS) & AUDIO_BIT) == enabled) return 1;
        if ((ULONG)(rd(TIMER) - start) >= 100000) break;
    }
    return 0;
}

/* Read-only property requests, BCM2711 mailbox at bus 0x7e00b880.
 * Use mailbox.resource when available; the direct path is only for systems
 * without it. Do not consume another client's response. A submitted buffer
 * remains allocated on timeout, because VideoCore can still write into it.
 */
static int direct_property(uint32_t command[8])
{
    APTR allocation;
    ULONG *request;
    ULONG length = 32, address, start, spins, i;
    int sent = 0, received = 0;
    if (SysBase->LibNode.lib_Version < 37) {
        puts("Direct mailbox requires Exec V37+ for cache/DMA functions.");
        return 0;
    }
    allocation = AllocMem(128, MEMF_PUBLIC | MEMF_FAST | MEMF_CLEAR);
    if (!allocation) { puts("Cannot allocate mailbox request."); return 0; }
    request = (ULONG *)(((ULONG)allocation + 63) & ~63ul);
    for (i = 0; i < 8; ++i) request[i] = __builtin_bswap32(command[i]);
    address = (ULONG)CachePreDMA(request, &length, 0);
    /* This fallback supports identity-mapped Emu68 RAM in the first GiB.
     * Never truncate a higher RAM address to a VideoCore bus alias. */
    if (length < 32 || address != (ULONG)request ||
        address < 0x08000000ul || address > 0x3fffffe0ul || (address & 15)) {
        printf("Mailbox buffer not in supported low RPi RAM: %08x (%u bytes).\n", address, length);
        CachePostDMA(request, &length, 0);
        FreeMem(allocation, 128);
        return 0;
    }
    printf("Direct mailbox request: tag=%08x buffer=%08x\n", command[2], address);
    fflush(stdout);
    /* Other Amiga tasks and interrupt handlers must not interleave mailbox
     * traffic during this short polled transaction. All exits are balanced.
     * Limit interrupt masking to 100 ms, with an independent spin bound. */
    Forbid();
    Disable();
    if ((rd(0xb898) & (1ul << 30)) && !(rd(0xb8b8) & (1ul << 31))) {
        wr(0xb8a0, address | 8);
        sent = 1;
        start = rd(TIMER);
        for (spins = 0; spins < 2000000; ++spins) {
            if (!(rd(0xb898) & (1ul << 30))) {
                if (rd(0xb890) == (address | 8)) {
                    (void)rd(0xb880);
                    received = 1;
                }
                break;
            }
            if ((ULONG)(rd(TIMER) - start) >= 100000) break;
        }
    }
    Enable();
    Permit();
    if (sent && !received) {
        puts("Mailbox reply timed out or belongs to another client. No foreign reply consumed.");
        puts("128-byte pending request retained; reboot before retrying.");
        return 0;
    }
    length = 32;
    CachePostDMA(request, &length, 0);
    if (received)
        for (i = 0; i < 8; ++i) command[i] = __builtin_bswap32(request[i]);
    FreeMem(allocation, 128);
    if (!sent) puts("Mailbox busy; request not submitted.");
    return received;
}

static ULONG query_bvb(void)
{
    static const uint32_t tags[] = {0x00030047u, 0x00030002u};
    unsigned i;
    if (MailboxBase) {
        const struct Library *lib = MailboxBase;
        printf("mailbox.resource: V%u.%u, vectors=%u bytes\n",
               (unsigned)lib->lib_Version, (unsigned)lib->lib_Revision,
               (unsigned)lib->lib_NegSize);
        if (lib->lib_NegSize < 6) { puts("No raw property vector."); return 0; }
    } else {
        puts("mailbox.resource not installed; using direct property mailbox.");
    }
    for (i = 0; i < 2; ++i) {
        uint32_t request[8] = {32, 0, tags[i], 8, 0, 14, 0, 0};
        ULONG rate;
        if (MailboxBase) {
            MB_RawCommand((ULONG *)request);
            if (request[0] == 0xffffffffu) {
                puts("mailbox.resource transaction failed; no second request sent.");
                return 0;
            }
        } else if (!direct_property(request)) return 0;
        printf("Clock tag=%08x response=%08x length=%08x id=%u rate=%u\n",
               (unsigned)tags[i], (unsigned)request[1], (unsigned)request[4],
               (unsigned)request[5], (unsigned)request[6]);
        rate = clock_reply(request, tags[i]);
        if (rate) {
            puts(i == 0 ? "Using measured BVB clock." : "Using firmware-reported BVB clock.");
            return rate;
        }
    }
    return 0;
}
static const ULONG saved_regs[] = {
    THRESH, FORMAT, RATE, MAP, CONFIG, PACKET, CRP, CTS0, CTS1, PHY,
    RAMAUDIO, RAMAUDIO+4, RAMAUDIO+8, RAMAUDIO+12, RAMAUDIO+16,
    RAMAUDIO+20, RAMAUDIO+24, RAMAUDIO+28, RAMAUDIO+32
};
#define SAVED_COUNT (sizeof(saved_regs) / sizeof(saved_regs[0]))
static ULONG saved[SAVED_COUNT], original_ramcfg, original_ctrl;

static int restore_state(int resume_audio)
{
    unsigned i;
    int ok;
    wr(CTRL, IDLE_CONTROL);
    wr(RAMCFG, original_ramcfg & ~AUDIO_BIT);
    ok = wait_audio(0);
    wr(CTRL, 1);
    wr(CTRL, 2);
    wr(CTRL, 1ul << 9);
    /* Do not overwrite packet RAM if the packet engine failed to stop. */
    for (i = 0; i < SAVED_COUNT; ++i) {
        if (saved_regs[i] >= RAMAUDIO && saved_regs[i] <= RAMAUDIO+32 && !ok)
            continue;
        wr(saved_regs[i], saved[i]);
    }
    wr(CTRL, original_ctrl & ((15ul << 4) | (1ul << 12) | (1ul << 13) | (1ul << 15)));
    wr(RAMCFG, resume_audio ? original_ramcfg : original_ramcfg & ~AUDIO_BIT);
    if (!wait_audio(resume_audio && !!(original_ramcfg & AUDIO_BIT))) ok = 0;
    return ok;
}

static int restore(void) { return restore_state(1); }

static int prepare_audio(ULONG bvb)
{
    ULONG start;
    unsigned i;
    original_ramcfg = rd(RAMCFG);
    original_ctrl = rd(CTRL);
    if (!(original_ramcfg & PACKETS_ENABLED)) {
        puts("HDMI0 packet engine is off. Connect an audio-capable HDMI display before boot.");
        return 20;
    }
    /* Firmware can leave an audio infoframe enabled even when MAI is idle.
     * The user's first probe has RAM_CFG=0x10015 and MAI_CTL=0x200.
     * Only an enabled/busy audio FIFO indicates an existing audio stream. */
    if (original_ctrl & (ENABLE | (1ul << 14))) {
        puts("HDMI0 audio is already active; test did not change registers.");
        return 20;
    }
    start = rd(TIMER);
    Delay(1);
    if (rd(TIMER) == start) { puts("Hardware timer did not advance."); return 20; }
    for (i = 0; i < SAVED_COUNT; ++i) saved[i] = rd(saved_regs[i]);
    wr(RAMCFG, original_ramcfg & ~AUDIO_BIT);
    if (!wait_audio(0)) { puts("Audio infoframe stop timed out."); goto failed; }
    wr(CTRL, 1 | (1ul << 9) | IDLE_CONTROL);
    /* Circle's BCM2711 audio clock assumption: 108 MHz / 48 kHz = 2250. */
    wr(RATE, 2250ul << 8);
    wr(FORMAT, (2ul << 16) | (9ul << 8));
    wr(THRESH, 0x10101010);
    wr(CONFIG, (1ul << 26) | (1ul << 27) | 3);
    wr(MAP, 0x10);
    wr(PACKET, (1ul << 29) | (1ul << 24) | (15ul << 10) | 3);
    wr(CRP, (1ul << 24) | 6144);
    wr(CTS0, (bvb + 500) / 1000);
    wr(CTS1, (bvb + 500) / 1000);
    wr(RAMAUDIO, 0x0a0184);
    wr(RAMAUDIO+4, 0x0170);
    for (i = 8; i <= 32; i += 4) wr(RAMAUDIO+i, 0);
    wr(RAMCFG, original_ramcfg | AUDIO_BIT);
    if (!wait_audio(1)) { puts("Audio infoframe start timed out."); goto failed; }
    wr(PHY, rd(PHY) & ~(1ul << 4));
    return 0;
failed:
    if (!restore()) { puts("Audio restoration timed out; reboot before retrying."); return 21; }
    return 20;
}

static ULONG query_dma_mask(void)
{
    uint32_t command[8] = {32, 0, 0x60001, 4, 0, 0, 0, 0};
    ULONG mask;
    APTR key;
    if (MailboxBase && ((struct Library *)MailboxBase)->lib_NegSize >= 6)
        MB_RawCommand((ULONG *)command);
    else if (!direct_property(command)) return 0;
    if (command[0] != 32 || command[1] != 0x80000000u ||
        command[2] != 0x60001 || command[3] != 4 ||
        !(command[4] & 0x80000000u) || (command[4] & 0x7fffffffu) < 4 ||
        command[6] != 0) {
        puts("Firmware did not return a valid DMA channel mask.");
        return 0;
    }
    mask = command[5];
    printf("Firmware DMA mask: %08x\n", mask);
    key = DT_OpenKey((CONST_STRPTR)"/soc/dma@7e007000");
    if (key) {
        mask &= cell(key, "brcm,dma-channel-mask", 0xffffffffu);
        DT_CloseKey(key);
    }
    mask &= 0xffu; /* The legacy DMA engines supported by Circle on RPi4. */
    printf("Allowed legacy DMA channels 0..7: %02x\n", mask);
    return mask;
}

/* Allocate a cache-line-aligned buffer only in the low identity-mapped RPi
 * Fast RAM window. Walking Exec's free lists and AllocAbs are protected
 * against task switches/interrupt allocations. No memory is removed by hand.
 */
static APTR allocate_dma_memory(ULONG size)
{
    struct MemHeader *mh;
    APTR result = NULL;
    Forbid();
    Disable();
    for (mh = (struct MemHeader *)SysBase->MemList.lh_Head;
         mh->mh_Node.ln_Succ && !result;
         mh = (struct MemHeader *)mh->mh_Node.ln_Succ) {
        struct MemChunk *chunk;
        if (!(mh->mh_Attributes & MEMF_FAST)) continue;
        for (chunk = mh->mh_First; chunk; chunk = chunk->mc_Next) {
            ULONG start = (ULONG)chunk, end;
            if (start >= 0x40000000u || chunk->mc_Bytes > 0xffffffffu-start) continue;
            end = start + chunk->mc_Bytes;
            if (end > 0x40000000u) end = 0x40000000u;
            if (start < 0x08000000u) start = 0x08000000u;
            start = (start + 63u) & ~63u;
            if (end >= start && end-start >= size) {
                result = AllocAbs(size, (APTR)start);
                if (result) break;
            }
        }
    }
    Enable();
    Permit();
    return result;
}

static int wait_dma_reset(ULONG base)
{
    ULONG start = rd(TIMER), spins;
    for (spins = 0; spins < 2000000; ++spins) {
        if (!(rd(base) & DMA_CS_BUSY)) return 1;
        if ((ULONG)(rd(TIMER)-start) >= 100000) break;
    }
    return 0;
}

static int stop_dma(int channel, ULONG original_enable, ULONG original_cs)
{
    ULONG base = 0x7000u + (ULONG)channel * 0x100u;
    wr(base, DMA_CS_RESET);
    if (!wait_dma_reset(base)) return 0;
    wr(base+4, 0);
    wr(base+0x20, 7); /* clear DMA error flags */
    wr(base, original_cs & 0x30ff0000u); /* idle channel's priority/control fields */
    Forbid();
    Disable();
    wr(0x7ff0, (rd(0x7ff0) & ~(1u << channel)) | (original_enable & (1u << channel)));
    Enable();
    Permit();
    return 1;
}

/* Publish prepared data only to the inactive buffer. The short copy/cache
 * operation cannot be preempted by an Amiga task or interrupt handler.
 * Generation of samples and all waiting happen outside this critical section.
 * Returns 1 on success, 2 if no write was attempted (retry is safe),
 * 0 if a copy crossed its deadline, -1 on cache/address failure. */
static __attribute__((unused)) int stream_publish(ULONG base, ULONG bus, uint8_t *plan, unsigned target,
                          const uint8_t *staging, ULONG *duration)
{
    ULONG before, available, length, address, current;
    int status = 2;
    uint8_t *destination = plan+STREAM_DATA_OFFSET+target*STREAM_BUFFER_BYTES;
    Forbid();
    Disable();
    before = rd(TIMER);
    current = rd(base+4);
    available = rd(base+0x14);
    if ((rd(base) & DMA_CS_ACTIVE) &&
        stream_can_publish(current, bus, target, available) && rd(base+4) == current) {
        status = 0;
        length = STREAM_BUFFER_BYTES;
        CachePostDMA(destination, &length, DMA_ReadFromRAM);
        memcpy(destination, staging, STREAM_BUFFER_BYTES);
        length = STREAM_BUFFER_BYTES;
        address = (ULONG)CachePreDMA(destination, &length, DMA_ReadFromRAM);
        *duration = rd(TIMER)-before;
        if (length != STREAM_BUFFER_BYTES || address != (ULONG)destination) status = -1;
        else if (rd(base+4) == current && *duration < (available*125u)/48u &&
                 *duration < STREAM_PUBLISH_LIMIT_US) status = 1;
    }
    *duration = rd(TIMER)-before;
    Enable();
    Permit();
    return status;
}
