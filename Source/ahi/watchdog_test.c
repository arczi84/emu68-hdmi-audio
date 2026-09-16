/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "watchdog.h"
int main(void) {
    unsigned fault;
    assert(STREAM_PERIOD_US==96000 && STREAM_BUFFER_FRAMES==4608);
    assert(hdmi_observation_fault(1000,21000,DMA_CS_ACTIVE,0)==0);
    assert(hdmi_observation_fault(1000,96999,DMA_CS_ACTIVE,0)==0);
    fault=hdmi_observation_fault(1000,97000,DMA_CS_ACTIVE,0);
    assert(fault==3 && hdmi_retry_allowed(fault,1,0,0));
    assert(hdmi_observation_fault(0xfffffff0u,0xfffffff0u+96000u,DMA_CS_ACTIVE,0)==3);
    assert(hdmi_observation_fault(0,120000,DMA_CS_ACTIVE|DMA_CS_ERROR,0)==2);
    assert(hdmi_observation_fault(0,120000,DMA_CS_ACTIVE,1)==2);
    assert(hdmi_observation_fault(0,120000,DMA_CS_ACTIVE,2)==2);
    assert(hdmi_observation_fault(0,120000,DMA_CS_ACTIVE,4)==2);
    assert(hdmi_observation_fault(0,1000,0,0)==2);
    for(fault=0;fault<=17;++fault) {
        assert(!hdmi_retry_allowed(fault,0,0,0));
        assert(!hdmi_retry_allowed(fault,1,1,0));
        assert(!hdmi_retry_allowed(fault,1,0,1));
        if(fault!=3 && fault!=4) assert(!hdmi_retry_allowed(fault,1,0,0));
    }
    assert(hdmi_retry_allowed(4,1,0,0));
    puts("PASS: 96 ms deadline, wrap, DMA faults take precedence; STOP and unsafe cleanup block retry.");
    return 0;
}
