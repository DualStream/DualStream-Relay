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

#include "ladder-ts.h"

/* Shared between the packet writer and the program table writer. */

#define DSR_TS_PID_PAT 0x0000
#define DSR_TS_PID_PMT 0x1000
#define DSR_TS_PID_NULL 0x1FFF
#define DSR_TS_PID_VIDEO_BASE 0x0200
#define DSR_TS_PID_AUDIO 0x0101

struct dsr_ts_stream {
	uint16_t pid;
	uint8_t continuity;
};

struct dsr_ts_mux {
	dsr_ts_sink_fn sink;
	void *ctx;

	struct dsr_ts_stream video[DSR_TS_MAX_VIDEO];
	uint8_t *video_headers[DSR_TS_MAX_VIDEO];
	size_t video_header_sizes[DSR_TS_MAX_VIDEO];
	bool video_header_missing_noted[DSR_TS_MAX_VIDEO];
	size_t video_count;

	struct dsr_ts_stream audio;
	bool have_audio;
	uint8_t adts_profile;
	uint8_t adts_rate_index;
	uint8_t adts_channels;

	uint8_t pat_continuity;
	uint8_t pmt_continuity;
	bool tables_written;
	int64_t tables_at;

	uint8_t chunk[DSR_TS_CHUNK_SIZE];
	size_t chunk_len;

	uint8_t *scratch;
	size_t scratch_size;
	bool failed;
};

/* Append one 188-byte packet; a full chunk goes to the sink. */
bool dsr_ts_emit_packet(struct dsr_ts_mux *mux, const uint8_t *packet);

/* The program association and map tables, one packet each. */
bool dsr_ts_write_tables(struct dsr_ts_mux *mux);
