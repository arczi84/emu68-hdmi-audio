#include "hdmi_backend.h"
static const char version[] __attribute__((used)) = "$VER: HDMITest 0.6 (12.09.2026)";
static int tone(ULONG bvb)
{
    /* The default Amiga Shell stack may be only 4 KiB. */
    static uint32_t blocks[3][384];
    ULONG start, elapsed, sub = 0, stalls = 0, status = 0, spins = 0;
    ULONG last_progress;
    unsigned stage, frame;
    BYTE priority;
    struct Task *task = FindTask(NULL);
    int result = 0;
    for (stage = 0; stage < 3; ++stage)
        for (frame = 0; frame < 192; ++frame) {
            int32_t sample = (int32_t)sine48[frame % 48] * 256;
            blocks[stage][2*frame] = __builtin_bswap32(iec958(stage == 1 ? 0 : sample, frame));
            blocks[stage][2*frame+1] = __builtin_bswap32(iec958(stage == 0 ? 0 : sample, frame));
        }
    if (prepare_audio(bvb)) return 20;
    puts("Playing 1 kHz, -30 dBFS: LEFT 1s, RIGHT 1s, BOTH 1s. Ctrl-C stops.");
    fflush(stdout);
    priority = SetTaskPri(task, 5);
    wr(CTRL, (2ul << 4) | (1ul << 12) | (1ul << 13) | ENABLE);
    start = last_progress = rd(TIMER);
    while (sub < 288000) {
        ULONG control = rd(CTRL);
        status |= control;
        if (!(control & FULL)) {
            stage = sub / 96000;
            *(volatile ULONG *)(io + DATA) = blocks[stage][sub % 384];
            __asm__ volatile ("nop" ::: "memory");
            ++sub;
            spins = 0;
            if ((sub & 31) == 0) last_progress = rd(TIMER);
        } else {
            ++stalls;
            if (++spins >= 2000000 || (ULONG)(rd(TIMER)-last_progress) > 100000) {
                result = 20;
                break;
            }
        }
        if ((sub & 255) == 0) {
            if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) { result = 5; break; }
            if ((ULONG)(rd(TIMER)-start) > 4500000) { result = 20; break; }
        }
    }
    elapsed = rd(TIMER)-start;
    if (!result && (elapsed < 2800000 || elapsed > 4500000)) result = 20;
    /* Keep supplying digital silence briefly, so queued tone samples can drain. */
    if (!result) {
        ULONG tail = rd(TIMER), count = 0;
        while ((ULONG)(rd(TIMER)-tail) < 10000 && count++ < 2000000) {
            if (!(rd(CTRL) & FULL)) {
                wr(DATA, iec958(0, (sub / 2) % 192));
                ++sub;
            }
        }
    }
    wr(CTRL, IDLE_CONTROL);
    SetTaskPri(task, priority);
    printf("Stream: %u us; FIFO-full polls: %u; observed MAI: %08x\n",
           elapsed, stalls, status);
    if (status & 4) puts("MAI empty-error observed (startup or underrun). Polling is not a final driver.");
    if (result == 5) puts("Stopped by Ctrl-C.");
    if (result == 20) puts("FIFO watchdog expired or stream duration was outside 2.8..4.5 s.");
    if (!result) puts("Sample stream sent. Confirm audible tone/channel order by listening.");
    if (!restore()) { puts("Audio state restoration timed out; reboot before another test."); result = 20; }
    else puts("Saved audio register configuration restored.");
    return result;
}

static int dma_tone(ULONG bvb)
{
    uint8_t *plan;
    ULONG allowed, physical, length, original_enable = 0, base = 0;
    ULONG channels_cs[8], channels_cb[8], channels_debug[8];
    ULONG start = 0, elapsed = 0, cs = 0, debug = 0, mai = 0, polls = 0;
    ULONG final_cb = 0, remaining = 0, silence_cb = 0;
    struct dma_tail_watch tail = {0, 0};
    const char *reason = NULL;
    int channel = -1, configured = 0, completed = 0, result = 20, quiet = 1;
    unsigned i, stage, frame;
    allowed = query_dma_mask();
    if (!allowed) { puts("No firmware-approved legacy DMA channel available."); return 20; }
    plan = allocate_dma_memory(DMA_PLAN_BYTES);
    if (!plan) { puts("Cannot allocate aligned low RPi Fast RAM for DMA."); return 20; }
    memset(plan, 0, DMA_PLAN_BYTES);
    for (stage = 0; stage < DMA_SAMPLE_BLOCKS; ++stage)
        for (frame = 0; frame < 192; ++frame) {
            int32_t sample = (int32_t)sine48[frame % 48] * 256;
            if (stage >= 4) sample = dma_fade_sample(sample, (stage-4u)*192u + frame);
            uint8_t *p = plan + DMA_SAMPLE_OFFSET + stage*DMA_AUDIO_BLOCK_BYTES + frame*8u;
            dma_put_le32(p, iec958(stage == 1 || stage == 3 ? 0 : sample, frame));
            dma_put_le32(p+4, iec958(stage == 0 || stage == 3 ? 0 : sample, frame));
        }
    if (!dma_build_chain(plan, (ULONG)plan)) {
        puts("Unsupported DMA plan address.");
        FreeMem(plan, DMA_PLAN_BYTES);
        return 20;
    }
    length = DMA_PLAN_BYTES;
    physical = (ULONG)CachePreDMA(plan, &length, DMA_ReadFromRAM);
    if (length != DMA_PLAN_BYTES || physical != (ULONG)plan) {
        puts("DMA plan is not physically contiguous identity-mapped RPi RAM.");
        CachePostDMA(plan, &length, DMA_ReadFromRAM);
        FreeMem(plan, DMA_PLAN_BYTES);
        return 20;
    }
    printf("DMA plan: CPU=%08x bus=%08x bytes=%u CBs=%u DREQ=10\n",
           (unsigned)physical, (unsigned)(physical | 0xc0000000u),
           DMA_PLAN_BYTES, DMA_CB_COUNT);
    fflush(stdout);
    Forbid();
    Disable();
    original_enable = rd(0x7ff0);
    for (i = 0; i < 8; ++i) {
        ULONG offset = 0x7000u + i*0x100u;
        channels_cs[i] = rd(offset);
        channels_cb[i] = rd(offset+4);
        channels_debug[i] = rd(offset+0x20);
    }
    channel = dma_choose_channel(allowed, (uint32_t *)channels_cs,
                                 (uint32_t *)channels_cb, (uint32_t *)channels_debug);
    if (channel >= 0) wr(0x7ff0, original_enable | (1u << channel));
    Enable();
    Permit();
    if (channel < 0) {
        puts("All approved DMA channels are busy, configured or have pending errors/interrupts.");
        for (i = 0; i < 8; ++i) if (allowed & (1u << i))
            printf("DMA%u CS=%08x CB=%08x DEBUG=%08x\n", i, channels_cs[i], channels_cb[i], channels_debug[i]);
        goto cleanup;
    }
    printf("Selected DMA channel %d (original CS=%08x DEBUG=%08x).\n",
           channel, channels_cs[channel], channels_debug[channel]);
    base = 0x7000u + (ULONG)channel*0x100u;
    Delay(1); /* allow the enabled channel's clock to settle */
    wr(base, DMA_CS_RESET);
    if (!wait_dma_reset(base)) { puts("DMA reset timed out."); goto cleanup; }
    if (prepare_audio(bvb)) goto cleanup;
    configured = 1;
    puts("DMA: LEFT 1s, RIGHT 1s, BOTH 1s with fade-out, then 200 ms silence.");
    fflush(stdout);
    wr(base+0x20, 7);
    wr(base+4, physical | 0xc0000000u);
    silence_cb = (physical + DMA_TONE_BLOCKS*DMA_CB_BYTES) | 0xc0000000u;
    start = rd(TIMER);
    wr(base, DMA_CS_START);
    wr(CTRL, (2ul << 4) | (1ul << 12) | (1ul << 13) | ENABLE);
    for (polls = 0; polls < 300; ++polls) {
        cs = rd(base);
        debug = rd(base+0x20);
        mai |= rd(CTRL);
        elapsed = rd(TIMER)-start;
        if ((cs & DMA_CS_ERROR) || (debug & 7u)) { reason = "DMA controller reported an error."; break; }
        if (dma_completed(cs, debug) || !(cs & DMA_CS_ACTIVE)) {
            reason = "DMA stopped unexpectedly before controlled silence shutdown."; break;
        }
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) { result = 5; reason = "Stopped by Ctrl-C."; break; }
        if (elapsed > 5000000) { reason = "DMA playback watchdog expired."; break; }
        if (dma_tail_ready(&tail, rd(base+4), silence_cb, rd(TIMER))) {
            completed = 1; result = 0; break;
        }
        Delay(1); /* no FIFO polling, no raised priority, no DMA interrupt */
    }
    /* Stop the HDMI consumer while DMA is still feeding zero PCM. There
     * must be no printf/Delay gap with an enabled MAI and an exhausted FIFO. */
    wr(CTRL, IDLE_CONTROL);
    final_cb = rd(base+4);
    remaining = rd(base+0x14);
    if (polls == 300) reason = "DMA observation limit reached.";
    if (completed && (elapsed < 3150000 || elapsed > 4500000)) {
        reason = "Unexpected DMA duration; expected about 3.2 seconds."; result = 20;
    }
cleanup:
    if (configured) wr(CTRL, IDLE_CONTROL);
    if (channel >= 0) quiet = stop_dma(channel, original_enable, channels_cs[channel]);
    if (configured) {
        if (!restore()) { puts("Audio state restoration timed out; reboot before retrying."); result = 20; }
        else puts("Saved audio register configuration restored.");
        printf("DMA result: time=%u us checks=%u CS=%08x DEBUG=%08x MAI=%08x\n",
               elapsed, polls+1, cs, debug, mai);
        printf("DMA before stop: CB=%08x remaining=%u (silence loop expected)\n", final_cb, remaining);
        if (reason) puts(reason);
        if (!result && quiet) puts("DMA tone finished; silence supplied until audio was disabled.");
    }
    if (quiet) {
        length = DMA_PLAN_BYTES;
        CachePostDMA(plan, &length, DMA_ReadFromRAM);
        FreeMem(plan, DMA_PLAN_BYTES);
        if (channel >= 0) puts("DMA stopped; buffer freed; channel enable state restored.");
    } else {
        /* Never free memory that a DMA engine might still read. */
        puts("DMA did not quiesce. Buffer retained; reboot before retrying.");
        result = 20;
    }
    return result;
}

static int stream_tone(ULONG bvb)
{
    uint8_t *plan = NULL, *staging = NULL;
    ULONG mask, length, physical, bus, base = 0, original_enable = 0;
    ULONG cs_array[8], cb_array[8], debug_array[8];
    ULONG start = 0, last_seen = 0, elapsed = 0, now, gap;
    ULONG cs = 0, debug = 0, refills = 0, max_gap = 0, max_fill = 0, max_publish = 0;
    ULONG quiet_since = 0, sequence[2] = {0, 1}, playing_sequence = 0, next_sequence = 2;
    struct stream_source source = {0, 0};
    int channel = -1, configured = 0, quiet = 1, result = 20, active = 0, silence_seen = 0;
    unsigned i, checks = 0, deadline_errors = 0;
    const char *reason = NULL;
    mask = query_dma_mask();
    if (!mask) { puts("No firmware-approved legacy DMA channel available."); return 20; }
    plan = allocate_dma_memory(STREAM_PLAN_BYTES);
    staging = AllocMem(STREAM_BUFFER_BYTES, MEMF_PUBLIC | MEMF_FAST);
    if (!plan || !staging) {
        puts("Cannot allocate two DMA buffers and producer staging buffer.");
        if (plan) FreeMem(plan, STREAM_PLAN_BYTES);
        if (staging) FreeMem(staging, STREAM_BUFFER_BYTES);
        return 20;
    }
    memset(plan, 0, STREAM_PLAN_BYTES);
    stream_fill(plan+STREAM_DATA_OFFSET, &source);
    stream_fill(plan+STREAM_DATA_OFFSET+STREAM_BUFFER_BYTES, &source);
    if (!stream_build_ring(plan, (ULONG)plan)) {
        puts("Unsupported stream DMA address.");
        FreeMem(plan, STREAM_PLAN_BYTES);
        FreeMem(staging, STREAM_BUFFER_BYTES);
        return 20;
    }
    length = STREAM_PLAN_BYTES;
    physical = (ULONG)CachePreDMA(plan, &length, DMA_ReadFromRAM);
    if (physical != (ULONG)plan || length != STREAM_PLAN_BYTES) {
        puts("Stream DMA memory is not contiguous and identity mapped.");
        CachePostDMA(plan, &length, DMA_ReadFromRAM);
        FreeMem(plan, STREAM_PLAN_BYTES);
        FreeMem(staging, STREAM_BUFFER_BYTES);
        return 20;
    }
    bus = physical | 0xc0000000u;
    now = rd(TIMER);
    stream_fill(staging, &source); /* next chunk ready before DMA starts */
    max_fill = rd(TIMER)-now;
    printf("STREAM: 2 x %u bytes / 96 ms; CPU=%08x bus=%08x\n",
           STREAM_BUFFER_BYTES, physical, bus);
    Forbid();
    Disable();
    original_enable = rd(0x7ff0);
    for (i = 0; i < 8; ++i) {
        ULONG offset = 0x7000u+i*0x100u;
        cs_array[i] = rd(offset);
        cb_array[i] = rd(offset+4);
        debug_array[i] = rd(offset+0x20);
    }
    channel = dma_choose_channel(mask, (uint32_t *)cs_array,
                                 (uint32_t *)cb_array, (uint32_t *)debug_array);
    if (channel >= 0) wr(0x7ff0, original_enable | (1u << channel));
    Enable();
    Permit();
    if (channel < 0) { reason = "No idle unconfigured DMA channel."; goto cleanup; }
    printf("STREAM uses DMA channel %d; no IRQ handler; observation every DOS tick.\n", channel);
    base = 0x7000u+(ULONG)channel*0x100u;
    Delay(1);
    wr(base, DMA_CS_RESET);
    if (!wait_dma_reset(base)) { reason = "DMA reset timed out."; goto cleanup; }
    if (prepare_audio(bvb)) goto cleanup;
    configured = 1;
    puts("STREAM: 12s stereo, 500/750/1000/1500 Hz changing every second. Ctrl-C stops.");
    fflush(stdout);
    wr(base+0x20, 7);
    wr(base+4, bus);
    start = last_seen = rd(TIMER);
    wr(base, DMA_CS_START);
    wr(CTRL, (2ul << 4) | (1ul << 12) | (1ul << 13) | ENABLE);
    for (checks = 0; checks < 2000; ++checks) {
        int current;
        cs = rd(base); debug = rd(base+0x20);
        now = rd(TIMER); elapsed = now-start; gap = now-last_seen;
        if (gap > max_gap) max_gap = gap;
        if ((cs & DMA_CS_ERROR) || (debug & 7u) || !(cs & DMA_CS_ACTIVE)) {
            reason = "DMA stopped or reported a controller error."; break;
        }
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) { result = 5; reason = "Stopped by Ctrl-C."; break; }
        if (elapsed > 15000000u) { reason = "Stream watchdog expired."; break; }
        if (!stream_observation_safe(last_seen, now)) {
            ++deadline_errors; reason = "Observation gap >= 96 ms; buffer ownership uncertain."; break;
        }
        current = stream_active_buffer(rd(base+4), bus);
        if (current < 0) continue; /* a transient CB fetch: retry without sleeping */
        last_seen = now;
        if (current != active) {
            unsigned target = (unsigned)current ^ 1u;
            ULONG duration;
            int published;
            if (sequence[current] != playing_sequence+1 || next_sequence != sequence[current]+1) {
                ++deadline_errors; reason = "Buffer sequence mismatch."; break;
            }
            playing_sequence = sequence[current];
            published = stream_publish(base, bus, plan, target, staging, &duration);
            if (duration > max_publish) max_publish = duration;
            if (published != 1) {
                ++deadline_errors;
                reason = published < 0 ? "Refill cache/address validation failed." :
                                         "Refill deadline missed; stream stopped.";
                break;
            }
            sequence[target] = next_sequence++;
            active = current;
            ++refills;
            now = rd(TIMER);
            stream_fill(staging, &source);
            duration = rd(TIMER)-now;
            if (duration > max_fill) max_fill = duration;
        }
        if (playing_sequence >= STREAM_TOTAL_FRAMES/STREAM_BUFFER_FRAMES) {
            now = rd(TIMER);
            if (!silence_seen) { silence_seen = 1; quiet_since = now; }
            if ((ULONG)(now-quiet_since) >= 200000u) { result = 0; break; }
        }
        Delay(1);
    }
    if (checks == 2000) reason = "Stream observation limit reached.";
    wr(CTRL, IDLE_CONTROL);
    elapsed = rd(TIMER)-start;
cleanup:
    if (configured) wr(CTRL, IDLE_CONTROL);
    if (channel >= 0) quiet = stop_dma(channel, original_enable, cs_array[channel]);
    if (configured && !restore()) { reason = "Audio state restoration timed out; reboot."; result = 20; }
    if (reason) puts(reason);
    if (configured) {
        printf("STREAM result: time=%u us refills=%u deadline_errors=%u\n", elapsed, refills, deadline_errors);
        printf("Max: producer=%u us publish=%u us observation_gap=%u us\n", max_fill, max_publish, max_gap);
        printf("DMA last CS=%08x DEBUG=%08x; playing chunk=%u\n", cs, debug, playing_sequence);
        if (!result && quiet) puts("STREAM completed: new audio data supplied continuously to alternating buffers.");
    }
    FreeMem(staging, STREAM_BUFFER_BYTES);
    if (quiet) {
        length = STREAM_PLAN_BYTES;
        CachePostDMA(plan, &length, DMA_ReadFromRAM);
        FreeMem(plan, STREAM_PLAN_BYTES);
        if (channel >= 0) puts("DMA stopped; stream buffers freed; channel enable state restored.");
    } else {
        puts("DMA did not quiesce; stream DMA buffers retained. Reboot before retrying.");
        result = 20;
    }
    return result;
}

int main(void)
{
    int play = 0, use_dma = 0, use_stream = 0, result;
    ULONG bvb = 0;
    LONG options[5] = {0, 0, 0, 0, 0};
    struct SignalSemaphore gate;
    struct RDArgs source = {0};
    struct RDArgs *parsed;
    STRPTR raw;
    puts("HDMITest 0.6 - Emu68 / BCM2711 / HDMI0 / polling + DMA + STREAM");
    if (((struct Library *)DOSBase)->lib_Version < 37) {
        puts("Requires dos.library V37+ (AmigaOS 2.04+).");
        return 20;
    }
    /* Parse the original DOS command line rather than libnix's argv.
     * An explicit source also avoids an already-consumed CLI input stream. */
    raw = GetArgStr();
    source.RDA_Source.CS_Buffer = raw ? raw : (STRPTR)"";
    source.RDA_Source.CS_Length = strlen((const char *)source.RDA_Source.CS_Buffer);
    source.RDA_Flags = RDAF_NOPROMPT;
    parsed = ReadArgs((CONST_STRPTR)"TONE/S,BVBHZ/K/N,HELP/S,DMA/S,STREAM/S", options, &source);
    if (!parsed) {
        LONG error = IoErr();
        printf("Argument parsing failed (DOS error %d). Raw arguments: [%s]\n",
               (int)error, (const char *)source.RDA_Source.CS_Buffer);
        PrintFault(error, (CONST_STRPTR)"HDMITest");
        puts("Usage: HDMITest [TONE | DMA | STREAM] [BVBHZ=clock_Hz] [HELP]");
        return 20;
    }
    use_dma = options[3] != 0;
    use_stream = options[4] != 0;
    play = options[0] != 0 || use_dma || use_stream;
    if (options[1]) bvb = (ULONG)*(LONG *)options[1];
    if (options[2]) {
        FreeArgs(parsed);
        puts("Usage: HDMITest [TONE | DMA | STREAM] [BVBHZ=clock_Hz] [HELP]");
        puts("TONE: working FIFO test. DMA: buffered 3-second HDMI0 test.");
        puts("STREAM: 12-second test with two continuously refilled DMA buffers.");
        return 0;
    }
    if (options[1] && (bvb < 1000000 || bvb > 340000000)) {
        FreeArgs(parsed);
        puts("BVBHZ must be 1000000..340000000 Hz.");
        return 20;
    }
    FreeArgs(parsed);
    printf("Mode: %s\n", use_stream ? "STREAM" : (use_dma ? "DMA" : (play ? "TONE" : "diagnostic")));
    if (!discover()) return 20;
    printf("Peripheral base: %08x\n", io);
    printf("HDMI0 RAM_CFG=%08x RAM_STATUS=%08x MAI_CTL=%08x\n",
           rd(RAMCFG), rd(RAMSTATUS), rd(CTRL));
    printf("MAI_RATE=%08x MAI_FMT=%08x CRP=%08x CTS0=%08x\n",
           rd(RATE), rd(FORMAT), rd(CRP), rd(CTS0));
    MailboxBase = OpenResource((CONST_STRPTR)"mailbox.resource");
    if (bvb) puts("Using explicit BVB clock override.");
    else bvb = query_bvb();
    printf("BVB clock: %u Hz; audio clock assumption: 108000000 Hz\n", bvb);
    if (!play) {
        puts("Diagnostic finished; no direct HDMI register writes performed.");
        puts("To play: HDMITest TONE, DMA or STREAM.");
        return 0;
    }
    if (!bvb) {
        puts("Cannot determine BVB clock from firmware.");
        puts("Save this output. BVBHZ override requires the actual BVB clock, not display pixel clock.");
        return 20;
    }
    /* Keep concurrent copies of this test from configuring the same device.
     * This is not a system-wide DMA allocator for unrelated drivers. */
    memset(&gate, 0, sizeof(gate));
    InitSemaphore(&gate);
    gate.ss_Link.ln_Name = "HDMITest.hardware";
    Forbid();
    if (FindSemaphore((CONST_STRPTR)"HDMITest.hardware")) {
        Permit();
        puts("Another HDMITest owns the hardware.");
        return 20;
    }
    AddSemaphore(&gate);
    Permit();
    result = use_stream ? stream_tone(bvb) : (use_dma ? dma_tone(bvb) : tone(bvb));
    Forbid();
    RemSemaphore(&gate);
    Permit();
    return result;
}
