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
#include "refill.h"
#include "lifetime.h"
#include "status.h"
#include "watchdog.h"
#include "guard.h"

struct DriverBase { struct Library lib; BPTR segment; };
struct Playback {
    struct SignalSemaphore gate;
    struct AHIAudioCtrlDrv *ctrl;
    struct hdmi_process process;
    volatile ULONG command, running, stop_requested;
    ULONG error, bvb, mix_bytes;
    void *mix;
    uint8_t *mix_storage;
    struct pcm_cursor pcm;
    volatile ULONG ready;
};
static struct Playback *owner;
static int poisoned;
static volatile ULONG diag_stage,diag_fault,diag_ctrl,diag_dma,diag_result;
static volatile ULONG max_gap,total_refills;
static volatile ULONG fault_cs,fault_debug,fault_cb;
static volatile ULONG diag_mix_bits;
static volatile ULONG resync_count,last_resync_reason;
static void snapshot_failure(ULONG base) {
    fault_cs=rd(base); fault_debug=rd(base+0x20); fault_cb=rd(base+4);
}
static LONG worker_entry(void);
static const char name[] = "emu68hdmi.audio";
static const char ident[] = "emu68hdmi.audio 4.16 (13.09.2026)\r\n";
const char ver[] __attribute__((used)) = "$VER: emu68hdmi.audio 4.16 (13.09.2026)";

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
    base->lib.lib_Version=4; base->lib.lib_Revision=16;
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
    if (base->lib.lib_OpenCnt || owner || poisoned) {
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
ULONG driver_alloc(struct AHIAudioCtrlDrv *ctrl)
{
    struct Playback *p;
    if (owner || poisoned || !(ctrl->ahiac_Flags & AHIACF_STEREO) ||
        (ctrl->ahiac_Flags & (AHIACF_MULTICHANNEL|AHIACF_RECORD))) return AHISF_ERROR;
    p=AllocVec(sizeof(*p),MEMF_PUBLIC|MEMF_CLEAR);
    if (!p) return AHISF_ERROR;
    InitSemaphore(&p->gate); p->gate.ss_Link.ln_Name="HDMITest.hardware";
    Forbid();
    if (owner || FindSemaphore((CONST_STRPTR)"HDMITest.hardware")) {
        Permit(); FreeVec(p); return AHISF_ERROR;
    }
    AddSemaphore(&p->gate); owner=p; Permit();
    ctrl->ahiac_DriverData=p; p->ctrl=ctrl;
    io=0;
    if (!discover()) return AHISF_ERROR;
    /* Require the serialized resource in the driver; no direct mailbox fallback. */
    MailboxBase=OpenResource((CONST_STRPTR)"mailbox.resource");
    if (!MailboxBase || ((struct Library *)MailboxBase)->lib_NegSize<6) return AHISF_ERROR;
    p->bvb=query_bvb();
    if (!p->bvb) return AHISF_ERROR;
    ctrl->ahiac_MixFreq=48000;
    return AHISF_MIXING|AHISF_TIMING|AHISF_KNOWSTEREO|AHISF_KNOWHIFI;
}

/* Preserve partially consumed AHI blocks across hardware DMA boundaries. AHI
 * owns BuffSamples: never replace it with our fixed hardware buffer size. */
static int next_pcm(void *context,const void **samples,unsigned *count)
{
    struct Playback *p=context;
    struct AHIAudioCtrlDrv *c=p->ctrl;
    Forbid();
    if(p->stop_requested) { Permit(); return 0; }
    diag_stage=20;
    if(!pcm_guard_ok(p->mix_storage,p->mix_bytes)) { diag_fault=1; Permit(); return 0; }
    CallHookPkt(c->ahiac_PlayerFunc,c,NULL);
    if(c->ahiac_BuffType!=(p->pcm.hifi?AHIST_S32S:AHIST_S16S) || !c->ahiac_BuffSamples ||
       c->ahiac_BuffSamples>p->mix_bytes/(p->pcm.hifi?8u:4u)) { diag_fault=1; Permit(); return 0; }
    *count=c->ahiac_BuffSamples;
    diag_stage=21;
    CallHookPkt(c->ahiac_MixerFunc,c,p->mix);
    diag_stage=22;
    if(!pcm_guard_ok(p->mix_storage,p->mix_bytes)) { diag_fault=1; Permit(); return 0; }
    *samples=p->mix;
    Permit();
    return 1;
}
static ULONG playback(struct Playback *p)
{
    uint8_t *plan=NULL,*staging=NULL;
    struct MsgPort *timer_port=NULL;
    struct timerequest *timer=NULL;
    int timer_open=0;
    struct pcm_pacer pacer;
    struct ahi_refill refill={0};
    ULONG mask, length, physical, bus=0, base=0, original_enable=0;
    ULONG cs[8], cb[8], debug[8], last_seen, now;
    int channel=-1, configured=0, quiet=1;
    ULONG result=AHIE_UNKNOWN;
    unsigned i;
    if(p->stop_requested) return AHIE_OK;
    diag_stage=9;
    timer_port=CreateMsgPort();
    if(!timer_port) { diag_fault=HDMI_FAULT_TIMER_PORT; goto done; }
    timer=(struct timerequest *)CreateIORequest(timer_port,sizeof(*timer));
    if(!timer) { diag_fault=HDMI_FAULT_TIMER_REQUEST; goto done; }
    if(OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_MICROHZ,(struct IORequest *)timer,0)) {
        diag_fault=HDMI_FAULT_TIMER_OPEN; goto done;
    }
    timer_open=1;
    diag_stage=10;
    mask=query_dma_mask();
    if (!mask) { diag_fault=11; goto done; }
    diag_stage=11;
    plan=allocate_dma_memory(STREAM_PLAN_BYTES);
    staging=AllocMem(STREAM_BUFFER_BYTES,MEMF_PUBLIC|MEMF_FAST);
    if (!plan || !staging) { diag_fault=12; result=AHIE_NOMEM; goto done; }
    memset(plan,0,STREAM_PLAN_BYTES);
    /* Start DMA with valid IEC958 silence. Do not consume 288 ms of AHI
     * input before the client can run and replenish its linked writes. */
    pcm_silence(plan+STREAM_DATA_OFFSET,2u*STREAM_BUFFER_FRAMES);
    if(!stream_build_ring(plan,(ULONG)plan)) goto done;
    diag_stage=12;
    length=STREAM_PLAN_BYTES;
    physical=(ULONG)CachePreDMA(plan,&length,DMA_ReadFromRAM);
    if (physical!=(ULONG)plan || length!=STREAM_PLAN_BYTES) { diag_fault=13; goto done; }
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
    diag_stage=15;
    { int prepared=prepare_audio(p->bvb);
      if(prepared) { diag_fault=15; if(prepared==21) poisoned=1; goto done; } }
    diag_stage=16;
    configured=1;
    wr(base+0x20,7); wr(base+4,bus);
    last_seen=rd(TIMER); wr(base,DMA_CS_START);
    wr(CTRL,(2ul<<4)|(1ul<<12)|(1ul<<13)|ENABLE);
    pcm_pacer_start(&pacer,last_seen);
    Forbid(); p->ready=1; p->error=AHIE_OK;
    if(p->process.waiter) Signal(p->process.waiter,p->process.wake);
    Permit();
    for (;;) {
        int current;
        unsigned late=0;
        diag_stage=30;
        ULONG duration;
        if(p->stop_requested || (SetSignal(0,0)&SIGBREAKF_CTRL_C)) { result=AHIE_OK; break; }
        now=rd(TIMER);
        if((ULONG)(now-last_seen)>max_gap) max_gap=now-last_seen;
        {
            ULONG observed_cs=rd(base), observed_debug=rd(base+0x20);
            ULONG fault=hdmi_observation_fault(last_seen,now,observed_cs,observed_debug);
            if(fault==HDMI_FAULT_GAP) {
                late=1; last_resync_reason=HDMI_FAULT_GAP;
            } else if(fault) {
                diag_fault=fault; fault_cs=observed_cs; fault_debug=observed_debug;
                fault_cb=rd(base+4); break;
            }
        }
        current=stream_active_buffer(rd(base+4),bus);
        if(current<0) continue;
        last_seen=now;
        ahi_refill_observe(&refill,&pacer,&p->pcm,current,late,now);
        if(refill.pending && pacer.filled!=STREAM_BUFFER_FRAMES && !refill.resync) {
            last_resync_reason=HDMI_FAULT_MIX_DEADLINE;
            ahi_refill_defer(&refill,&pacer,&p->pcm,now);
        }
        if(!pcm_pacer_pump(&pacer,&p->pcm,staging,STREAM_BUFFER_FRAMES,
                           now,next_pcm,p)) break;
        if(refill.pending && pacer.filled==STREAM_BUFFER_FRAMES) {
            int published=stream_publish(base,bus,plan,(unsigned)current^1u,staging,&duration);
            if(published==2) {
                /* No DMA memory was touched. Wait for a safe inactive half. */
                if(!refill.resync) last_resync_reason=HDMI_FAULT_PUBLISH;
                ahi_refill_defer(&refill,&pacer,&p->pcm,rd(TIMER));
            } else if(published!=1) {
                diag_fault=published<0?HDMI_FAULT_CACHE:HDMI_FAULT_PUBLISH;
                snapshot_failure(base); break;
            } else {
                ++total_refills;
                if(refill.resync) ++resync_count;
                ahi_refill_published(&refill,&pacer,&p->pcm,rd(TIMER));
            }
        }
        timer->tr_node.io_Command=TR_ADDREQUEST;
        timer->tr_time.tv_secs=0;
        timer->tr_time.tv_micro=HDMI_POLL_US;
        /* Synchronous request is completed before STOP cleanup can free it. */
        if(DoIO((struct IORequest *)timer)) { diag_fault=HDMI_FAULT_TIMER_IO; break; }
    }
done:
    diag_stage=40;
    if(configured) wr(CTRL,IDLE_CONTROL);
    if(channel>=0) quiet=stop_dma(channel,original_enable,cs[channel]);
    if(base) diag_dma=rd(base);
    diag_stage=41;
    if(configured && !restore_state(0)) { diag_fault=5; poisoned=1; result=AHIE_UNKNOWN; }
    if(configured) diag_ctrl=rd(CTRL);
    diag_stage=42;
    if(staging) FreeMem(staging,STREAM_BUFFER_BYTES);
    if(plan && quiet) { length=STREAM_PLAN_BYTES; CachePostDMA(plan,&length,DMA_ReadFromRAM); FreeMem(plan,STREAM_PLAN_BYTES); }
    if(!quiet) { diag_fault=6; poisoned=1; result=AHIE_UNKNOWN; }
    if(timer_open) CloseDevice((struct IORequest *)timer);
    if(timer) DeleteIORequest((struct IORequest *)timer);
    if(timer_port) DeleteMsgPort(timer_port);
    diag_result=result;
    return result;
}
/* STOP acknowledges an idle worker, not a process exit. DOS termination is
 * reserved for FreeAudio and acknowledged through NP_ExitCode. */
static LONG worker_entry(void)
{
    struct Playback *p=FindTask(NULL)->tc_UserData;
    for(;;) {
        ULONG command,result;
        Forbid();
        while(!p->command) { diag_stage=50; Wait(SIGBREAKF_CTRL_D); }
        command=p->command; p->command=0;
        Permit();
        if(command==2) { diag_stage=62; return 0; }
        SetSignal(0,SIGBREAKF_CTRL_C);
        /* Same playback lifetime as 4.7: one run per AHI START. Re-entering
         * playback after a deadline fault is disabled while investigating
         * the Wipeout regression reported with 4.10. Preserve the fault. */
        result=playback(p);
        Forbid();
        p->error=result; p->running=0;
        if(p->process.waiter) Signal(p->process.waiter,p->process.wake);
        Permit();
        /* After the acknowledgement, do not touch PCM or the AHI control
         * until START supplies a fresh buffer and wakes this worker. */
    }
}
/* GCC 6.5.0b miscompiles a bit test of the nonvolatile stack argument:
 * btst #232,8(a5) tests the high byte instead of PLAY in the low byte.
 * A volatile argument avoids that faulty optimization (correct low-byte test).
 * Keep the native
 * -66 vector regression test: STOP must set stop_requested for flags=1. */
void driver_stop(volatile ULONG flags,struct AHIAudioCtrlDrv *ctrl)
{
    struct Playback *p=ctrl->ahiac_DriverData;
    BYTE signal;
    if(!p || !(flags&AHISF_PLAY)) return;
    signal=AllocSignal(-1);
    Forbid();
    p->stop_requested=1;
    p->process.waiter=signal>=0?FindTask(NULL):NULL;
    p->process.wake=signal>=0?1UL<<signal:0;
    if(p->running && p->process.task)
        Signal((struct Task *)p->process.task,SIGBREAKF_CTRL_C);
    while(p->running) {
        if(signal>=0) Wait(p->process.wake); else Delay(1);
    }
    p->process.waiter=NULL;
    Permit();
    if(signal>=0) FreeSignal(signal);
    if(p->mix_storage) FreeVec(p->mix_storage);
    p->mix_storage=NULL;
    p->mix=NULL; memset(&p->pcm,0,sizeof(p->pcm));
}
ULONG driver_start(ULONG flags,struct AHIAudioCtrlDrv *ctrl)
{
    struct Playback *p=ctrl->ahiac_DriverData;
    ULONG result;
    BYTE signal;
    diag_stage=1;
    if(!p || poisoned || (flags&AHISF_RECORD)) return AHIE_UNKNOWN;
    if(!(flags&AHISF_PLAY)) return AHIE_OK;
    driver_stop(AHISF_PLAY,ctrl);
    if((ctrl->ahiac_BuffType!=AHIST_S16S && ctrl->ahiac_BuffType!=AHIST_S32S) || !ctrl->ahiac_BuffSize ||
        !ctrl->ahiac_PlayerFunc || !ctrl->ahiac_MixerFunc) return AHIE_UNKNOWN;
    signal=AllocSignal(-1);
    if(signal<0) return AHIE_NOMEM;
    p->mix_bytes=ctrl->ahiac_BuffSize;
    if(p->mix_bytes>0x100000UL) { FreeSignal(signal); diag_fault=1; return AHIE_UNKNOWN; }
    p->mix_storage=AllocVec(p->mix_bytes+2*PCM_GUARD_BYTES,MEMF_PUBLIC|MEMF_CLEAR);
    if(!p->mix_storage) { FreeSignal(signal); return AHIE_NOMEM; }
    pcm_guard_init(p->mix_storage,p->mix_bytes);
    p->mix=p->mix_storage+PCM_GUARD_BYTES;
    p->pcm.hifi=(ctrl->ahiac_BuffType==AHIST_S32S);
    diag_mix_bits=p->pcm.hifi?32:16;
    diag_fault=0; diag_stage=2;
    max_gap=total_refills=0;
    resync_count=last_resync_reason=0;
    fault_cs=fault_debug=fault_cb=0;
    Forbid();
    p->ready=0; p->error=AHIE_UNKNOWN; p->stop_requested=0;
    p->running=1; p->command=1;
    p->process.waiter=FindTask(NULL); p->process.wake=1UL<<signal;
    if(p->process.task) Signal((struct Task *)p->process.task,SIGBREAKF_CTRL_D);
    else if(!hdmi_spawn(&p->process,worker_entry,p)) {
        diag_fault=16; p->running=0; p->command=0;
    }
    while(p->running && !p->ready) Wait(p->process.wake);
    p->process.waiter=NULL;
    result=p->error; Permit();
    FreeSignal(signal);
    if(result!=AHIE_OK) driver_stop(AHISF_PLAY,ctrl);
    return result;
}
void driver_free(struct AHIAudioCtrlDrv *ctrl)
{
    struct Playback *p=ctrl->ahiac_DriverData;
    BYTE signal;
    if(!p) return;
    diag_stage=60;
    driver_stop(AHISF_PLAY,ctrl);
    signal=AllocSignal(-1);
    Forbid();
    p->process.waiter=signal>=0?FindTask(NULL):NULL;
    p->process.wake=signal>=0?1UL<<signal:0;
    if(p->process.task) {
        diag_stage=61; p->command=2;
        Signal((struct Task *)p->process.task,SIGBREAKF_CTRL_D);
        /* Exit callback runs after DOS cleanup and keeps scheduling forbidden
         * until DOS removes the task. Only then may context/library be freed. */
        while(p->process.task) {
            if(signal>=0) Wait(p->process.wake); else Delay(1);
        }
    }
    p->process.waiter=NULL;
    diag_stage=63;
    if(!poisoned) { RemSemaphore(&p->gate); owner=NULL; }
    ctrl->ahiac_DriverData=NULL; Permit();
    if(signal>=0) FreeSignal(signal);
    if(!poisoned) FreeVec(p);
}
void driver_disable(void) { Forbid(); }
void driver_enable(void) { Permit(); }
LONG driver_attr(ULONG attr,LONG arg,LONG def,struct AHIAudioCtrlDrv *ctrl)
{
    switch(attr) {
    case AHIDB_Bits: return ctrl && (ctrl->ahiac_Flags&AHIACF_HIFI)?24:16;
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
    case AHIDB_MaxPlaySamples: return 3*STREAM_BUFFER_FRAMES+(ctrl?ctrl->ahiac_MaxBuffSamples:65535);
    case AHIDB_Outputs: return 1;
    case AHIDB_Output: return (LONG)"Raspberry Pi 4 HDMI0";
    case AHIDB_Inputs: return 0;
    default: return def;
    }
}

/* Read-only snapshots. No live MMIO and no AHI calls from diagnostics. */
LONG driver_control(ULONG attr,LONG arg,struct AHIAudioCtrlDrv *ctrl)
{
    (void)arg; (void)ctrl;
    switch(attr) {
    case HDMI_QUERY_MIX_BITS: return diag_mix_bits;
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
        state=(poisoned?1:0) | (owner?2:0) |
            (owner && owner->process.task?4:0) | (owner && owner->running?8:0) |
            (owner && owner->stop_requested?16:0) | (owner?owner->command<<8:0);
        Permit();
        return state;
    }
    default: return 0;
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
