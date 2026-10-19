/* SPDX-License-Identifier: GPL-3.0-or-later
 * Experimental Emu68 BCM2711 HDMI0 AHI subdriver. ABI: AHI V4.
 * Mixer contract follows Martin Blom's AHI SDK (see vendor/ahi).
 */
#include <stdio.h>
#include <string.h>
#include <exec/resident.h>
#include <dos/dostags.h>
#include <devices/timer.h>
#include "buffering.h"
#include <libraries/ahi_sub.h>
#include <proto/utility.h>
#include <exec/interrupts.h>
#include <proto/gic400.h>
struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct Library *UtilityBase;
/* Library callbacks have no CLI output stream. */
#define printf(...) ((void)0)
#define puts(...) ((void)0)
#define fflush(...) ((void)0)
#include "../src/hdmi_backend.h"
#include "pcm.h"
#include "pacing.h"
#include "ring.h"
#include "lifetime.h"
#include "status.h"
#include "guard.h"

struct DriverBase { struct Library lib; BPTR segment; };
/* Shared with the interrupt handler. */
struct irq_ctx {
    volatile ULONG count, stamp, error, calls;
    ULONG base, mask;
    struct Task *task;
};
/* 4.18: up to PCM_MIX_MAX_CLIENTS AHI audio controls play at once (a game and a CD player, ...).
 * Each one is a Client with its own mix buffer and PCM cursor; the HDMI setup, the DMA ring, the
 * interrupt and the worker process are shared (struct Hardware). The worker sums the clients into
 * one 24 bit accumulator per segment (pcm_mix) and packs it with the master gain. The hardware
 * starts with the first client that plays and stops when the last one stops. */
struct Client {
    struct AHIAudioCtrlDrv *ctrl;
    const struct ring_geom *geom;       /* of the client's mode, used when it starts the hardware */
    ULONG mix_bytes, skip_mix;
    void *mix;
    uint8_t *mix_storage;
    struct pcm_cursor pcm;              /* input side only: block, position, hifi */
    /* active: the worker may mix it; busy: the worker is inside its hooks or buffer (set/cleared
     * under Forbid). STOP clears active and waits for busy=0 before the buffer is freed. */
    volatile ULONG active, busy, stop_requested, failed;
    struct Task *waiter;
    ULONG wake;
};
struct Hardware {
    struct Interrupt irq;
    struct irq_ctx ctx;
    struct SignalSemaphore gate;        /* "HDMITest.hardware" while any client is allocated */
    struct hdmi_process process;
    volatile ULONG command, running, stop_requested;
    const struct ring_geom *geom;
    ULONG error, bvb;
    int32_t *acc;                       /* seg_frames stereo frames, 24 bit sums */
    ULONG acc_bytes;
    struct pcm_cursor out;              /* packs acc: IEC958 frame counter and master gain ramp */
    struct Client *clients[PCM_MIX_MAX_CLIENTS];
    unsigned nclients;                  /* allocated controls; slots change under api_lock + Forbid */
    volatile ULONG ready;
};
static struct Hardware hw;
/* Serialises AllocAudio / FreeAudio / Start / Stop of all clients (task context only). */
static struct SignalSemaphore api_lock;
static int poisoned;
static volatile ULONG diag_stage,diag_fault,diag_ctrl,diag_dma,diag_result;
static volatile ULONG max_gap,total_refills;
static volatile ULONG fault_cs,fault_debug,fault_cb;
static volatile ULONG diag_mix_bits;
static volatile ULONG diag_irq_id,diag_irqs,wakes,fault_mai,fault_ramstatus;
static volatile ULONG max_burst;
struct Library *GIC400_Base;
static volatile ULONG pre_calls,pre_skips;
/* Master volume, AHI "Fixed" (0x10000 = unity): AHIC_OutputVolume of AHIsub_HardwareControl, applied when the samples are packed.
 * AHI Prefs shows it as the Output volume of the unit, and AHI sets it when a program allocates the unit's mode. */
static volatile ULONG master_gain=65536UL;
/* Calls ahiac_PreTimer / ahiac_PostTimer: A2 must hold the audio control (entry.S). */
extern ULONG hdmi_call_timer(APTR fn,APTR ctrl);
static volatile ULONG resync_count,last_resync_reason,last_resync_at;
static void snapshot_failure(ULONG base) {
    fault_cs=rd(base); fault_debug=rd(base+0x20); fault_cb=rd(base+4);
}
static LONG worker_entry(void);
static const char name[] = "emu68hdmi.audio";
static const char ident[] = "emu68hdmi.audio 4.18 (19.10.2026)\r\n";
const char ver[] __attribute__((used)) = "$VER: emu68hdmi.audio 4.18 (19.10.2026)";

struct DriverBase *driver_init(struct DriverBase *base, BPTR segment, struct ExecBase *exec)
{
    SysBase = exec;
    DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library",37);
    UtilityBase = OpenLibrary((CONST_STRPTR)"utility.library",37);
    if (!DOSBase || !UtilityBase) {
        if (DOSBase) CloseLibrary((struct Library *)DOSBase);
        if (UtilityBase) CloseLibrary((struct Library *)UtilityBase);
        FreeMem((UBYTE *)base-base->lib.lib_NegSize,base->lib.lib_NegSize+base->lib.lib_PosSize);
        return NULL;
    }
    base->segment=segment;
    base->lib.lib_Node.ln_Type=NT_LIBRARY;
    base->lib.lib_Node.ln_Name=(char *)name;
    base->lib.lib_Flags=LIBF_SUMUSED|LIBF_CHANGED;
    base->lib.lib_Version=4; base->lib.lib_Revision=18;
    InitSemaphore(&api_lock);
    InitSemaphore(&hw.gate); hw.gate.ss_Link.ln_Name="HDMITest.hardware";
    base->lib.lib_IdString=(APTR)ident;
    return base;
}
struct DriverBase *driver_open(struct DriverBase *base)
{
    ++base->lib.lib_OpenCnt;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return base;
}
BPTR driver_expunge(struct DriverBase *base)
{
    BPTR segment;
    if (base->lib.lib_OpenCnt || hw.nclients || poisoned) {
        base->lib.lib_Flags |= LIBF_DELEXP; return 0;
    }
    segment=base->segment;
    Remove((struct Node *)base);
    CloseLibrary((struct Library *)UtilityBase);
    CloseLibrary((struct Library *)DOSBase);
    FreeMem((UBYTE *)base-base->lib.lib_NegSize,base->lib.lib_NegSize+base->lib.lib_PosSize);
    return segment;
}
BPTR driver_close(struct DriverBase *base)
{
    if (base->lib.lib_OpenCnt) --base->lib.lib_OpenCnt;
    return base->lib.lib_Flags & LIBF_DELEXP ? driver_expunge(base) : 0;
}
/* Mode IDs 001f0010 / 001f0011 keep the long 2 x 96 ms ring of 4.16 (music,
 * background playback); 001f0012 / 001f0013 use the low latency ring. The ID is the
 * first tag AHI hands to AllocAudio and GetAttr (AHIDB_AudioID of the mode file). */
static const struct ring_geom geom_long={RING_LONG_SEGS,RING_LONG_SEG_FRAMES};
static const struct ring_geom geom_low={RING_LOW_SEGS,RING_LOW_SEG_FRAMES};
static const struct ring_geom *mode_geom(struct TagItem *tags)
{
    ULONG id=tags?GetTagData(AHIDB_AudioID,0,tags):0;
    return (id&0xffffUL)>=0x12UL?&geom_low:&geom_long;
}
/* The first client claims the hardware semaphore and discovers HDMI; later ones share it. A failed
 * discovery still registers the client: AHI calls FreeAudio, which must release it. */
ULONG driver_alloc(struct TagItem *tags,struct AHIAudioCtrlDrv *ctrl)
{
    struct Client *c;
    ULONG result=AHISF_ERROR;
    if (poisoned || !(ctrl->ahiac_Flags & AHIACF_STEREO) ||
        (ctrl->ahiac_Flags & (AHIACF_MULTICHANNEL|AHIACF_RECORD))) return AHISF_ERROR;
    c=AllocVec(sizeof(*c),MEMF_PUBLIC|MEMF_CLEAR);
    if (!c) return AHISF_ERROR;
    ObtainSemaphore(&api_lock);
    Forbid();
    if (hw.nclients>=PCM_MIX_MAX_CLIENTS ||
        (!hw.nclients && FindSemaphore((CONST_STRPTR)"HDMITest.hardware"))) {
        Permit(); ReleaseSemaphore(&api_lock); FreeVec(c); return AHISF_ERROR;
    }
    if (!hw.nclients) AddSemaphore(&hw.gate);
    hw.clients[hw.nclients++]=c;
    Permit();
    ctrl->ahiac_DriverData=c; c->ctrl=ctrl; c->geom=mode_geom(tags);
    if (!hw.bvb) {
        io=0;
        /* Require the serialized resource in the driver; no direct mailbox fallback. */
        if (discover()) {
            MailboxBase=OpenResource((CONST_STRPTR)"mailbox.resource");
            if (MailboxBase && ((struct Library *)MailboxBase)->lib_NegSize>=6) hw.bvb=query_bvb();
        }
    }
    if (hw.bvb) {
        ctrl->ahiac_MixFreq=48000;
        result=AHISF_MIXING|AHISF_TIMING|AHISF_KNOWSTEREO|AHISF_KNOWHIFI;
    }
    ReleaseSemaphore(&api_lock);
    return result;
}

/* Preserve partially consumed AHI blocks across hardware DMA boundaries. AHI
 * owns BuffSamples: never replace it with our fixed hardware buffer size. */
static int next_pcm(void *context,const void **samples,unsigned *count)
{
    struct Client *p=context;
    struct AHIAudioCtrlDrv *c=p->ctrl;
    Forbid();
    if(p->stop_requested || !p->active) { Permit(); return 0; }
    diag_stage=20;
    if(!pcm_guard_ok(p->mix_storage,p->mix_bytes)) { diag_fault=1; Permit(); return 0; }
    CallHookPkt(c->ahiac_PlayerFunc,c,NULL);
    if(c->ahiac_BuffType!=(p->pcm.hifi?AHIST_S32S:AHIST_S16S) || !c->ahiac_BuffSamples ||
       c->ahiac_BuffSamples>p->mix_bytes/(p->pcm.hifi?8u:4u)) { diag_fault=1; Permit(); return 0; }
    *count=c->ahiac_BuffSamples;
    diag_stage=21;
    if(p->skip_mix) memset(p->mix,0,c->ahiac_BuffSamples*(p->pcm.hifi?8u:4u));
    else CallHookPkt(c->ahiac_MixerFunc,c,p->mix);
    diag_stage=22;
    if(!pcm_guard_ok(p->mix_storage,p->mix_bytes)) { diag_fault=1; Permit(); return 0; }
    *samples=p->mix;
    Permit();
    return 1;
}
/* Fills one free ring segment: every active client is mixed into the accumulator, which is then
 * packed straight into the DMA segment (no Disable()). Returns 1, or -1 on a cache/address failure.
 * AHI's CPU overload protection (V4) is one Pre/PostTimer pair per client around its part of the
 * refill, as paula.audio does; when AHI asks to skip, that client's Player hook still runs but its
 * mixer is replaced by silence. A client whose hooks fail (diag_fault says why) is muted until its
 * next START; the others keep playing. */
static int fill_segment(uint8_t *dest,ULONG seg_bytes)
{
    const unsigned frames=hw.geom->seg_frames;
    ULONG length=seg_bytes,address;
    unsigned i;
    CachePostDMA(dest,&length,DMA_ReadFromRAM);
    memset(hw.acc,0,frames*8u);
    for(i=0;i<PCM_MIX_MAX_CLIENTS;++i) {
        struct Client *c;
        struct AHIAudioCtrlDrv *actrl;
        int ok;
        Forbid();
        c=hw.clients[i];
        if(!c || !c->active || c->failed) { Permit(); continue; }
        c->busy=1;
        Permit();
        actrl=c->ctrl;
        c->skip_mix=0;
        if(actrl->ahiac_PreTimer) {
            c->skip_mix=(hdmi_call_timer((APTR)actrl->ahiac_PreTimer,actrl)&0xffffUL)!=0;
            ++pre_calls;
            if(c->skip_mix) ++pre_skips;
        }
        ok=pcm_mix(&c->pcm,hw.acc,frames,next_pcm,c);
        if(actrl->ahiac_PostTimer) hdmi_call_timer((APTR)actrl->ahiac_PostTimer,actrl);
        Forbid();
        if(!ok && !c->stop_requested && c->active) c->failed=1;
        c->busy=0;
        if(c->waiter) Signal(c->waiter,c->wake);
        Permit();
    }
    pcm_mix_finish(hw.acc,frames*2u);
    hw.out.gain_target=master_gain;
    hw.out.samples=hw.acc; hw.out.position=0; hw.out.count=frames;
    pcm_pack(&hw.out,dest,frames);
    length=seg_bytes;
    address=(ULONG)CachePreDMA(dest,&length,DMA_ReadFromRAM);
    if(length!=seg_bytes || address!=(ULONG)dest) return -1;
    return 1;
}
/* The handler is called by gic400.library as a subroutine (A6 = SysBase,
 * A1 = is_Data) from the INTB_EXTER server chain. The line is level
 * triggered: the channel's INT flag must be cleared or it storms. The CS
 * write keeps ACTIVE and the priority fields, so the ring is never paused. */
static ULONG irq_handler(register struct irq_ctx *c asm("a1"))
{
    ULONG cs=rd(c->base);
    ++c->calls;
    if(!(cs&DMA_CS_INT)) return 0;
    if(cs&DMA_CS_ERROR) {
        wr(c->base,DMA_CS_INT);
        c->error=cs|1UL;
    } else {
        wr(c->base,DMA_CS_START|DMA_CS_INT);
    }
    c->stamp=rd(TIMER);
    ++c->count;
    Signal(c->task,c->mask);
    return 0;
}
/* GIC id of a legacy channel from the device tree: three cells per entry
 * (type, number, flags), a SPI (type 0) has GIC id number+32. Channels 0..6
 * have a private line; 7/8 and 9/10 share one, and gic400.library accepts a
 * single server per line. */
static ULONG dma_channel_irq(int channel)
{
    static const char *const nodes[]={"/soc/dma-controller@7e007000","/soc/dma@7e007000"};
    ULONG irq=0,length=0;
    unsigned n;
    for(n=0;n<2 && !irq;++n) {
        APTR key=DT_OpenKey((CONST_STRPTR)nodes[n]);
        const ULONG *cells;
        if(!key) continue;
        cells=prop(key,"interrupts",&length);
        if(cells && (ULONG)channel<length/12u && cells[3*channel]==0)
            irq=cells[3*channel+1]+32;
        DT_CloseKey(key);
    }
    return irq;
}
static ULONG playback(void)
{
    const struct ring_geom *g=hw.geom;
    const ULONG seg_bytes=ring_seg_bytes(g),data_off=ring_data_offset(g),plan_bytes=ring_plan_bytes(g);
    uint8_t *plan=NULL;
    struct MsgPort *timer_port=NULL;
    struct timerequest *timer=NULL;
    int timer_open=0, timer_pending=0;
    ULONG mask, length, physical, bus=0, base=0, original_enable=0;
    ULONG cs[8], cb[8], debug[8];
    ULONG irq_id=0, written=0, last_count=0, wait_mask;
    int channel=-1, configured=0, quiet=1, irq_added=0;
    BYTE irq_sig=-1;
    ULONG result=AHIE_UNKNOWN;
    unsigned i;
    if(hw.stop_requested) return AHIE_OK;
    diag_stage=9;
    /* The only timer left is a watchdog for a lost interrupt, 4 Hz. */
    timer_port=CreateMsgPort();
    if(!timer_port) { diag_fault=HDMI_FAULT_TIMER_PORT; goto done; }
    timer=(struct timerequest *)CreateIORequest(timer_port,sizeof(*timer));
    if(!timer) { diag_fault=HDMI_FAULT_TIMER_REQUEST; goto done; }
    if(OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_MICROHZ,(struct IORequest *)timer,0)) {
        diag_fault=HDMI_FAULT_TIMER_OPEN; goto done;
    }
    timer_open=1;
    diag_stage=10;
    mask=query_dma_mask()&0x7fu;
    if (!mask) { diag_fault=11; goto done; }
    diag_stage=11;
    plan=allocate_dma_memory(plan_bytes);
    if (!plan) { diag_fault=12; result=AHIE_NOMEM; goto done; }
    memset(plan,0,plan_bytes);
    /* Start DMA with valid IEC958 silence. Do not consume AHI input before
     * the client can run and replenish the ring. */
    for(i=0;i<g->segs;++i)
        pcm_silence(plan+data_off+i*seg_bytes,g->seg_frames);
    if(!ring_build(plan,(ULONG)plan,g)) goto done;
    diag_stage=12;
    length=plan_bytes;
    physical=(ULONG)CachePreDMA(plan,&length,DMA_ReadFromRAM);
    if (physical!=(ULONG)plan || length!=plan_bytes) { diag_fault=13; goto done; }
    bus=physical|0xc0000000u;
    diag_stage=13;
    Forbid(); Disable();
    original_enable=rd(0x7ff0);
    for(i=0;i<8;++i) { ULONG off=0x7000+i*0x100; cs[i]=rd(off); cb[i]=rd(off+4); debug[i]=rd(off+0x20); }
    channel=dma_choose_channel(mask,(uint32_t *)cs,(uint32_t *)cb,(uint32_t *)debug);
    if(channel>=0) wr(0x7ff0,original_enable|(1u<<channel));
    Enable(); Permit();
    if(channel<0) { diag_fault=14; goto done; }
    base=0x7000+(ULONG)channel*0x100;
    diag_stage=14;
    Delay(1); wr(base,DMA_CS_RESET);
    if(!wait_dma_reset(base)) { diag_fault=6; goto done; }
    wr(base,DMA_CS_INT|DMA_CS_END);
    diag_stage=15;
    { int prepared=prepare_audio(hw.bvb);
      if(prepared) { diag_fault=15; if(prepared==21) poisoned=1; goto done; } }
    diag_stage=16;
    configured=1;
    /* Interrupt server, before the first control block can complete. */
    irq_id=dma_channel_irq(channel);
    GIC400_Base=OpenLibrary((CONST_STRPTR)"gic400.library",1);
    if(!irq_id || !GIC400_Base ||
       (GIC400_Base->lib_Version==1 && GIC400_Base->lib_Revision<3)) {
        diag_fault=HDMI_FAULT_IRQ_SETUP; goto done;
    }
    irq_sig=AllocSignal(-1);
    if(irq_sig<0) { diag_fault=HDMI_FAULT_IRQ_SETUP; goto done; }
    hw.ctx.count=hw.ctx.stamp=hw.ctx.error=hw.ctx.calls=0;
    hw.ctx.base=base; hw.ctx.task=FindTask(NULL); hw.ctx.mask=1UL<<irq_sig;
    hw.irq.is_Node.ln_Type=NT_INTERRUPT; hw.irq.is_Node.ln_Pri=0;
    hw.irq.is_Node.ln_Name=(char *)name;
    hw.irq.is_Data=&hw.ctx; hw.irq.is_Code=(void (*)())irq_handler;
    if(AddIntServerEx(irq_id,0,FALSE,&hw.irq)) { diag_fault=HDMI_FAULT_IRQ_SETUP; goto done; }
    irq_added=1;
    diag_irq_id=irq_id;
    /* The low latency ring is only 48 ms deep: the first run of the mixer (Emu68 translates its code
     * on first use, ~30 ms) would lap it. Mix the whole ring before the DMA starts instead. The long ring
     * keeps the 4.16 behaviour, which starts with silence and has 96 ms of margin. */
    if(g==&geom_low) {
        for(i=0;i<g->segs;++i) {
            if(hw.stop_requested) { result=AHIE_OK; goto done; }
            if(fill_segment(plan+data_off+i*seg_bytes,seg_bytes)<0) { diag_fault=HDMI_FAULT_CACHE; goto done; }
        }
    }
    wr(base+0x20,7); wr(base+4,bus);
    wr(base,DMA_CS_START);
    wr(CTRL,(2ul<<4)|(1ul<<12)|(1ul<<13)|ENABLE);
    wait_mask=(1UL<<irq_sig)|SIGBREAKF_CTRL_C|(1UL<<timer_port->mp_SigBit);
    timer->tr_node.io_Command=TR_ADDREQUEST;
    timer->tr_time.tv_secs=0; timer->tr_time.tv_micro=HDMI_WATCHDOG_US;
    SendIO((struct IORequest *)timer); timer_pending=1;
    Forbid(); hw.ready=1; hw.error=AHIE_OK;
    if(hw.process.waiter) Signal(hw.process.waiter,hw.process.wake);
    Permit();
    for (;;) {
        ULONG sigs, completed, now;
        int slack;
        diag_stage=29;
        sigs=Wait(wait_mask);
        diag_stage=30;
        ++wakes;
        if(hw.stop_requested || (sigs&SIGBREAKF_CTRL_C)) { result=AHIE_OK; break; }
        if(sigs&(1UL<<timer_port->mp_SigBit)) {
            WaitIO((struct IORequest *)timer); timer_pending=0;
            if(hw.ctx.count==last_count) {
                diag_fault=HDMI_FAULT_IRQ_LOST; snapshot_failure(base);
                fault_mai=rd(CTRL); fault_ramstatus=rd(RAMSTATUS); break;
            }
            last_count=hw.ctx.count;
            /* timer.device consumes tr_time: refill it before every re-arm. */
            timer->tr_time.tv_secs=0; timer->tr_time.tv_micro=HDMI_WATCHDOG_US;
            SendIO((struct IORequest *)timer); timer_pending=1;
        }
        if(hw.ctx.error) {
            diag_fault=HDMI_FAULT_DMA; fault_cs=hw.ctx.error; fault_debug=rd(base+0x20);
            fault_cb=rd(base+4); break;
        }
        now=rd(TIMER);
        completed=hw.ctx.count;
        if(sigs&(1UL<<irq_sig)) { ULONG lag=now-hw.ctx.stamp; if(lag>max_gap) max_gap=lag; }
        diag_irqs=completed;
        for(;;) {
            slack=ring_slack(completed,written,g->segs);
            if(slack==RING_LAPPED) {
                ++resync_count; last_resync_reason=HDMI_FAULT_GAP; last_resync_at=completed;
                ring_resync(completed,&written,g->segs);
                continue;
            }
            if(slack==0) { ULONG burst=rd(TIMER)-now; if(burst>max_burst) max_burst=burst; break; }
            {
                uint8_t *dest=plan+data_off+(written%g->segs)*seg_bytes;
                if(hw.stop_requested) { result=AHIE_OK; goto done; }
                if(fill_segment(dest,seg_bytes)<0) { diag_fault=HDMI_FAULT_CACHE; snapshot_failure(base); goto done; }
                ++total_refills;
                completed=hw.ctx.count;
                /* The DMA reached this segment again while it was filled. */
                if(completed-written>=g->segs) {
                    ++resync_count; last_resync_reason=HDMI_FAULT_PUBLISH; last_resync_at=completed;
                }
                ++written;
            }
        }
    }
done:
    diag_stage=40;
    if(configured) wr(CTRL,IDLE_CONTROL);
    if(channel>=0) quiet=stop_dma(channel,original_enable,cs[channel]);
    /* Leave no INT flag behind a level triggered line, then drop the server. */
    if(base) wr(base,DMA_CS_INT|DMA_CS_END);
    if(irq_added) RemIntServerEx(irq_id,&hw.irq);
    if(base) diag_dma=rd(base);
    diag_stage=41;
    if(configured && !restore_state(0)) { diag_fault=5; poisoned=1; result=AHIE_UNKNOWN; }
    if(configured) diag_ctrl=rd(CTRL);
    diag_stage=42;
    if(irq_sig>=0) FreeSignal(irq_sig);
    if(GIC400_Base) { CloseLibrary(GIC400_Base); GIC400_Base=NULL; }
    if(plan && quiet) { length=plan_bytes; CachePostDMA(plan,&length,DMA_ReadFromRAM); FreeMem(plan,plan_bytes); }
    if(!quiet) { diag_fault=6; poisoned=1; result=AHIE_UNKNOWN; }
    if(timer_pending) { AbortIO((struct IORequest *)timer); WaitIO((struct IORequest *)timer); }
    if(timer_open) CloseDevice((struct IORequest *)timer);
    if(timer) DeleteIORequest((struct IORequest *)timer);
    if(timer_port) DeleteMsgPort(timer_port);
    diag_result=result;
    return result;
}
/* STOP acknowledges an idle worker, not a process exit. DOS termination is
 * reserved for the last FreeAudio and acknowledged through NP_ExitCode. */
static LONG worker_entry(void)
{
    for(;;) {
        ULONG command,result;
        Forbid();
        while(!hw.command) { diag_stage=50; Wait(SIGBREAKF_CTRL_D); }
        command=hw.command; hw.command=0;
        Permit();
        if(command==2) { diag_stage=62; return 0; }
        SetSignal(0,SIGBREAKF_CTRL_C);
        /* One playback run per hardware start (first client START). */
        result=playback();
        Forbid();
        hw.error=result; hw.running=0;
        if(hw.process.waiter) Signal(hw.process.waiter,hw.process.wake);
        Permit();
        /* After the acknowledgement, do not touch PCM or any AHI control
         * until a START restarts the hardware and wakes this worker. */
    }
}
/* Stops the shared hardware (caller holds api_lock, no client active). */
static void hw_stop(void)
{
    BYTE signal=AllocSignal(-1);
    Forbid();
    hw.stop_requested=1;
    hw.process.waiter=signal>=0?FindTask(NULL):NULL;
    hw.process.wake=signal>=0?1UL<<signal:0;
    if(hw.running && hw.process.task)
        Signal((struct Task *)hw.process.task,SIGBREAKF_CTRL_C);
    while(hw.running) {
        if(signal>=0) Wait(hw.process.wake); else Delay(1);
    }
    hw.process.waiter=NULL;
    Permit();
    if(signal>=0) FreeSignal(signal);
    if(hw.acc) FreeVec(hw.acc);
    hw.acc=NULL; hw.acc_bytes=0;
}
static int any_active(void)
{
    unsigned i;
    for(i=0;i<hw.nclients;++i) if(hw.clients[i]->active) return 1;
    return 0;
}
/* GCC 6.5.0b miscompiles a bit test of the nonvolatile stack argument:
 * btst #232,8(a5) tests the high byte instead of PLAY in the low byte.
 * A volatile argument avoids that faulty optimization (correct low-byte test).
 * Keep the native
 * -66 vector regression test: STOP must set stop_requested for flags=1. */
void driver_stop(volatile ULONG flags,struct AHIAudioCtrlDrv *ctrl)
{
    struct Client *c=ctrl->ahiac_DriverData;
    BYTE signal;
    if(!c || !(flags&AHISF_PLAY)) return;
    ObtainSemaphore(&api_lock);
    signal=AllocSignal(-1);
    Forbid();
    c->stop_requested=1; c->active=0;
    c->waiter=signal>=0?FindTask(NULL):NULL;
    c->wake=signal>=0?1UL<<signal:0;
    while(c->busy) {
        if(signal>=0) Wait(c->wake); else Delay(1);
    }
    c->waiter=NULL;
    Permit();
    if(signal>=0) FreeSignal(signal);
    if(!any_active()) hw_stop();
    if(c->mix_storage) FreeVec(c->mix_storage);
    c->mix_storage=NULL;
    c->mix=NULL; memset(&c->pcm,0,sizeof(c->pcm));
    ReleaseSemaphore(&api_lock);
}
ULONG driver_start(ULONG flags,struct AHIAudioCtrlDrv *ctrl)
{
    struct Client *c=ctrl->ahiac_DriverData;
    ULONG result=AHIE_OK;
    diag_stage=1;
    if(!c || poisoned || (flags&AHISF_RECORD)) return AHIE_UNKNOWN;
    if(!(flags&AHISF_PLAY)) return AHIE_OK;
    driver_stop(AHISF_PLAY,ctrl);
    if((ctrl->ahiac_BuffType!=AHIST_S16S && ctrl->ahiac_BuffType!=AHIST_S32S) || !ctrl->ahiac_BuffSize ||
        !ctrl->ahiac_PlayerFunc || !ctrl->ahiac_MixerFunc || !hw.bvb) return AHIE_UNKNOWN;
    ObtainSemaphore(&api_lock);
    c->mix_bytes=ctrl->ahiac_BuffSize;
    if(c->mix_bytes>0x100000UL) { diag_fault=1; ReleaseSemaphore(&api_lock); return AHIE_UNKNOWN; }
    c->mix_storage=AllocVec(c->mix_bytes+2*PCM_GUARD_BYTES,MEMF_PUBLIC|MEMF_CLEAR);
    if(!c->mix_storage) { ReleaseSemaphore(&api_lock); return AHIE_NOMEM; }
    pcm_guard_init(c->mix_storage,c->mix_bytes);
    c->mix=c->mix_storage+PCM_GUARD_BYTES;
    c->pcm.hifi=(ctrl->ahiac_BuffType==AHIST_S32S);
    c->stop_requested=0; c->failed=0;
    diag_mix_bits=c->pcm.hifi?32:16;
    if(hw.running) {
        /* The hardware already plays for another client: join at the next segment. */
        Forbid(); c->active=1; Permit();
    } else {
        BYTE signal=AllocSignal(-1);
        const struct ring_geom *g=c->geom;
        if(signal<0) { ReleaseSemaphore(&api_lock); driver_stop(AHISF_PLAY,ctrl); return AHIE_NOMEM; }
        /* A run that ended on a fault (no STOP) left its accumulator behind. */
        if(hw.acc) FreeVec(hw.acc);
        hw.geom=g;
        hw.acc_bytes=g->seg_frames*8u;
        hw.acc=AllocVec(hw.acc_bytes,MEMF_PUBLIC|MEMF_CLEAR);
        if(!hw.acc) { FreeSignal(signal); ReleaseSemaphore(&api_lock); driver_stop(AHISF_PLAY,ctrl); return AHIE_NOMEM; }
        memset(&hw.out,0,sizeof(hw.out));
        hw.out.hifi=1;
        /* Start at the master gain set now, not at unity: no swell when playback starts */
        hw.out.gain_enable=1; hw.out.gain_target=master_gain; hw.out.gain_goal=master_gain;
        hw.out.gain_cur=(int)(master_gain>>1)<<15; hw.out.gain_step=0; hw.out.ramp=0; hw.out.gain_init=1;
        diag_fault=0; diag_stage=2;
        max_gap=total_refills=wakes=diag_irqs=max_burst=pre_calls=pre_skips=0;
        resync_count=last_resync_reason=last_resync_at=0;
        fault_cs=fault_debug=fault_cb=0;
        Forbid();
        c->active=1;  /* before the ring is filled: the low latency ring mixes it before DMA starts */
        hw.ready=0; hw.error=AHIE_UNKNOWN; hw.stop_requested=0;
        hw.running=1; hw.command=1;
        hw.process.waiter=FindTask(NULL); hw.process.wake=1UL<<signal;
        if(hw.process.task) Signal((struct Task *)hw.process.task,SIGBREAKF_CTRL_D);
        else if(!hdmi_spawn(&hw.process,worker_entry,&hw)) {
            diag_fault=16; hw.running=0; hw.command=0;
        }
        while(hw.running && !hw.ready) Wait(hw.process.wake);
        hw.process.waiter=NULL;
        result=hw.error; Permit();
        FreeSignal(signal);
    }
    ReleaseSemaphore(&api_lock);
    if(result!=AHIE_OK) driver_stop(AHISF_PLAY,ctrl);
    return result;
}
void driver_free(struct AHIAudioCtrlDrv *ctrl)
{
    struct Client *c=ctrl->ahiac_DriverData;
    BYTE signal;
    unsigned i;
    if(!c) return;
    diag_stage=60;
    driver_stop(AHISF_PLAY,ctrl);
    ObtainSemaphore(&api_lock);
    Forbid();
    for(i=0;i<hw.nclients && hw.clients[i]!=c;++i) ;
    if(i<hw.nclients) {
        for(;i+1<hw.nclients;++i) hw.clients[i]=hw.clients[i+1];
        hw.clients[--hw.nclients]=NULL;
    }
    ctrl->ahiac_DriverData=NULL;
    Permit();
    if(!hw.nclients) {
        /* Last client: end the worker process and give the hardware back. */
        signal=AllocSignal(-1);
        Forbid();
        hw.process.waiter=signal>=0?FindTask(NULL):NULL;
        hw.process.wake=signal>=0?1UL<<signal:0;
        if(hw.process.task) {
            diag_stage=61; hw.command=2;
            Signal((struct Task *)hw.process.task,SIGBREAKF_CTRL_D);
            /* Exit callback runs after DOS cleanup and keeps scheduling forbidden
             * until DOS removes the task. Only then may context/library be freed. */
            while(hw.process.task) {
                if(signal>=0) Wait(hw.process.wake); else Delay(1);
            }
        }
        hw.process.waiter=NULL;
        if(!poisoned) RemSemaphore(&hw.gate);
        hw.bvb=0;
        Permit();
        if(signal>=0) FreeSignal(signal);
    }
    diag_stage=63;
    ReleaseSemaphore(&api_lock);
    FreeVec(c);
}
void driver_disable(void) { Forbid(); }
void driver_enable(void) { Permit(); }
LONG driver_attr(ULONG attr,LONG arg,LONG def,struct TagItem *tags,struct AHIAudioCtrlDrv *ctrl)
{
    switch(attr) {
    case AHIDB_Bits:
        /* The mode file says whether the mode is HiFi even when no control exists yet. */
        return (ctrl && (ctrl->ahiac_Flags&AHIACF_HIFI)) || (tags && GetTagData(AHIDB_HiFi,0,tags))?24:16;
    case AHIDB_Frequencies: return 1;
    case AHIDB_Frequency: return arg==0 ? 48000 : def;
    case AHIDB_Index: return 0;
    case AHIDB_Author: return (LONG)"Emu68 HDMI experimental driver";
    case AHIDB_Copyright: return (LONG)"GPL-3.0-or-later; HDMI setup based on Circle";
    case AHIDB_Version: return (LONG)ident;
    case AHIDB_Record: return FALSE;
    /* Compatibility flag requested for AHI Prefs' Unit 0-3 filter.
     * Playback is unchanged from 4.13: this does NOT provide AHI's ~10 ms
     * realtime latency guarantee; the output still has ~192 ms latency. */
    case AHIDB_Realtime: return TRUE;
    case AHIDB_MaxPlaySamples:
    {   /* Frames AHI must keep ahead for dynamic samples: the ring plus the block in progress. */
        const struct ring_geom *g=mode_geom(tags);
        return (g->segs+1)*g->seg_frames+(ctrl?ctrl->ahiac_MaxBuffSamples:65535);
    }
    /* The output volume can only attenuate: about -60 dB up to unity (a Fixed value, 0x10000 = 1.0). */
    case AHIDB_MinOutputVolume: return 0x41;
    case AHIDB_MaxOutputVolume: return 0x10000;
    case AHIDB_Outputs: return 1;
    case AHIDB_Output: return (LONG)"Raspberry Pi 4 HDMI0";
    case AHIDB_Inputs: return 0;
    default: return def;
    }
}

/* Read-only snapshots. No live MMIO and no AHI calls from diagnostics. */
LONG driver_control(ULONG attr,LONG arg,struct AHIAudioCtrlDrv *ctrl)
{
    (void)ctrl;
    switch(attr) {
    case AHIC_OutputVolume_Query: return (LONG)master_gain;
    case AHIC_OutputVolume: master_gain=arg<0?0UL:(arg>65536L?65536UL:(ULONG)arg); return TRUE;
    case HDMI_QUERY_MIX_BITS: return diag_mix_bits;
    case HDMI_QUERY_FEATURES: return 7; /* bit 0: interrupt driver, bit 1: master volume, bit 2: several clients */
    case HDMI_QUERY_MAX_BURST: return max_burst;
    case HDMI_QUERY_RESYNC_AT: return last_resync_at;
    case HDMI_QUERY_PRE_CALLS: return pre_calls;
    case HDMI_QUERY_PRE_SKIPS: return pre_skips;
    case HDMI_QUERY_FAULT_MAI: return fault_mai;
    case HDMI_QUERY_FAULT_RAMSTATUS: return fault_ramstatus;
    case HDMI_QUERY_IRQ_ID: return diag_irq_id;
    case HDMI_QUERY_IRQS: return diag_irqs;
    case HDMI_QUERY_WAKES: return wakes;
    case HDMI_QUERY_GAP: return max_gap;
    case HDMI_QUERY_FILLS: return total_refills;
    case HDMI_QUERY_RECOVERIES: return resync_count;
    case HDMI_QUERY_RECOVERY_FAULT: return last_resync_reason;
    case HDMI_QUERY_FAULT_CS: return fault_cs;
    case HDMI_QUERY_FAULT_DEBUG: return fault_debug;
    case HDMI_QUERY_FAULT_CB: return fault_cb;
    case HDMI_REQUEST_STALL: return 0; /* No automatic recovery in this build. */
    case HDMI_QUERY_STAGE: return diag_stage;
    case HDMI_QUERY_FAULT: return diag_fault;
    case HDMI_QUERY_CTRL: return diag_ctrl;
    case HDMI_QUERY_DMA: return diag_dma;
    case HDMI_QUERY_RESULT: return diag_result;
    case HDMI_QUERY_STATE: {
        ULONG state;
        Forbid();
        unsigned i,active=0,stopped=0;
        for(i=0;i<hw.nclients;++i) {
            if(hw.clients[i]->active) ++active;
            if(hw.clients[i]->stop_requested) stopped=1;
        }
        /* bits 0-4 and 8 as before 4.18 (16: a client was stopped); 12-14: allocated clients, 16-18: playing */
        state=(poisoned?1:0) | (hw.nclients?2:0) |
            (hw.process.task?4:0) | (hw.running?8:0) |
            (stopped?16:0) | (hw.command<<8) | (hw.nclients<<12) | (active<<16);
        Permit();
        return state;
    }
    default:
        return 0;
    }
}
extern void gw_init(void),gw_open(void),gw_close(void),gw_expunge(void),gw_null(void);
extern void gw_alloc(void),gw_free(void),gw_disable(void),gw_enable(void);
extern void gw_start(void),gw_stop(void),gw_attr(void),gw_unknown(void),gw_control(void);
static const APTR vectors[]={gw_open,gw_close,gw_expunge,gw_null,
    gw_alloc,gw_free,gw_disable,gw_enable,gw_start,gw_null,gw_stop,
    gw_unknown,gw_unknown,gw_unknown,gw_unknown,gw_unknown,gw_unknown,
    gw_attr,gw_control,(APTR)-1};
static const APTR init_table[]={(APTR)sizeof(struct DriverBase),(APTR)vectors,NULL,gw_init};
extern char _etext;
const struct Resident resident __attribute__((used))={RTC_MATCHWORD,
    (struct Resident *)&resident,(APTR)&_etext,RTF_AUTOINIT,4,NT_LIBRARY,0,
    (APTR)name,(APTR)ident,(APTR)init_table};
