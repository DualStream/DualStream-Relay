/*
DualStream Relay for OBS
Copyright (C) 2026 Dual Stream Studio Inc <hello@dualstream.gg>

SPDX-License-Identifier: GPL-2.0-or-later

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MPEG-TS writer for the relay contribution: up to ten H.264 video streams
 * and one AAC stream in one program, packed seven packets at a time into the
 * messages an SRT live link carries. Video i is pinned to PID 0x200 + i and
 * the audio to 0x101, which is the order the relay binds renditions by.
 *
 * Every timestamp handed in is in 90 kHz ticks and already offset so the
 * first decode time sits well above zero; the writer schedules its clock
 * reference behind the primary video's decode times from there. */

#define DSR_TS_PACKET_SIZE 188
#define DSR_TS_CHUNK_PACKETS 7
#define DSR_TS_CHUNK_SIZE (DSR_TS_PACKET_SIZE * DSR_TS_CHUNK_PACKETS)
#define DSR_TS_MAX_VIDEO 10

/* Receives every filled chunk. A false return ends the stream. */
typedef bool (*dsr_ts_sink_fn)(void *ctx, const uint8_t *chunk, size_t len);

struct dsr_ts_mux;

struct dsr_ts_mux *dsr_ts_mux_create(dsr_ts_sink_fn sink, void *ctx);
void dsr_ts_mux_destroy(struct dsr_ts_mux *mux);

/* Declare the streams, in ladder order, before the first packet. The video
 * parameter sets are the encoder's own Annex-B header, repeated ahead of any
 * keyframe that does not carry its own. The audio configuration is the two
 * byte AudioSpecificConfig; when the encoder gives none, the sample rate and
 * channel count stand in for it. */
bool dsr_ts_mux_add_video(struct dsr_ts_mux *mux, const uint8_t *parameter_sets, size_t size);
bool dsr_ts_mux_set_audio(struct dsr_ts_mux *mux, const uint8_t *config, size_t size, uint32_t sample_rate,
			  size_t channels);

bool dsr_ts_mux_write_video(struct dsr_ts_mux *mux, size_t index, const uint8_t *data, size_t size, int64_t pts,
			    int64_t dts, bool keyframe);
bool dsr_ts_mux_write_audio(struct dsr_ts_mux *mux, const uint8_t *data, size_t size, int64_t pts);

/* Send whatever is waiting in a partial chunk, padded with null packets. */
bool dsr_ts_mux_flush(struct dsr_ts_mux *mux);

#ifdef __cplusplus
}
#endif
