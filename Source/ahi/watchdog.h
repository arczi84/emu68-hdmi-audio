/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMI_WATCHDOG_H
#define HDMI_WATCHDOG_H
#include "../src/stream.h"
#include "../src/dma_control.h"
#include "status.h"
static inline unsigned hdmi_observation_fault(uint32_t previous,uint32_t now,
                                              uint32_t cs,uint32_t debug)
{
    /* A real hardware fault must not be masked by a simultaneous late poll. */
    if(!(cs&DMA_CS_ACTIVE) || (cs&DMA_CS_ERROR) || (debug&7u)) return HDMI_FAULT_DMA;
    if(!stream_observation_safe(previous,now)) return HDMI_FAULT_GAP;
    return HDMI_FAULT_NONE;
}
static inline int hdmi_retry_allowed(unsigned fault,int started,int stop,int poisoned)
{
    return started && !stop && !poisoned &&
        (fault==HDMI_FAULT_GAP || fault==HDMI_FAULT_PUBLISH);
}
#endif
