/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef HDMI_AHI_BUFFERING_H
#define HDMI_AHI_BUFFERING_H
/* DMA retains 4.11's 96 ms buffers. The mixer is paced independently by
 * AHI's sample count; the 1 ms timer only services that schedule and DMA. */
#define STREAM_BUFFER_FRAMES 4608u
#define STREAM_COPY_GUARD_BYTES 7680u
#define STREAM_PUBLISH_LIMIT_US 20000u
#define HDMI_POLL_US 1000u
#endif
