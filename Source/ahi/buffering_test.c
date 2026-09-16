/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "buffering.h"
#include "watchdog.h"
int main(void)
{
    static uint8_t ring[STREAM_PLAN_BYTES];
    unsigned i;
    const uint32_t physical=0x08001000u,bus=physical|0xc0000000u;
    assert(STREAM_PERIOD_US==96000u);
    assert(STREAM_COPY_GUARD_BYTES*1000000ULL/(STREAM_RATE*8)==STREAM_PUBLISH_LIMIT_US);
    assert(HDMI_POLL_US<STREAM_PUBLISH_LIMIT_US);
    assert(stream_build_ring(ring,physical));
    for(i=0;i<2;++i) {
        assert(!stream_can_publish(bus+32*i,bus,i,STREAM_BUFFER_BYTES));
        assert(!stream_can_publish(bus+32*i,bus,i^1u,STREAM_COPY_GUARD_BYTES-1));
        assert(stream_can_publish(bus+32*i,bus,i^1u,STREAM_COPY_GUARD_BYTES));
    }
    assert(!hdmi_observation_fault(1000,96999,DMA_CS_ACTIVE,0));
    assert(hdmi_observation_fault(1000,97000,DMA_CS_ACTIVE,0)==HDMI_FAULT_GAP);
    assert(hdmi_observation_fault(0xfffffff0u,0xfffffff0u+96000u,DMA_CS_ACTIVE,0)==HDMI_FAULT_GAP);
    assert(hdmi_observation_fault(0,97000,DMA_CS_ACTIVE|DMA_CS_ERROR,0)==HDMI_FAULT_DMA);
    puts("PASS: AHI 96 ms ring, inactive-buffer ownership, 20 ms copy guard, deadline and timer wrap.");
    return 0;
}
