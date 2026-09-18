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

#include <obs-module.h>
#include <util/deque.h>
#include <util/threading.h>

#include "ladder.h"
#include "ladder-srt.h"
#include "ladder-ts.h"

/* Shared between the output's lifecycle and its writer thread.
 *
 * Ownership is one-directional: the lifecycle side (start, stop, destroy,
 * all on threads OBS drives) creates the link, the muxer, the queue and the
 * writer thread, and is the only side that frees them or joins the writer.
 * The writer only consumes the queue and reports a lost link. */

struct dsr_queued_packet {
	uint8_t *data;
	size_t size;
	int64_t pts;
	int64_t dts;
	int32_t timebase_num;
	int32_t timebase_den;
	size_t track;
	bool video;
	bool keyframe;
};

struct dsr_ladder_output {
	obs_output_t *output;

	/* The start thread, and the flags that decide whether the lifecycle
	 * side joins it or it lets go of itself. */
	pthread_mutex_t start_mutex;
	pthread_t start_thread;
	bool connecting;
	volatile bool abort_start;

	pthread_t write_thread;
	bool write_thread_active;
	pthread_mutex_t queue_mutex;
	struct deque queue;
	os_sem_t *write_sem;
	os_event_t *stop_event;

	volatile bool active;
	volatile bool stopping;

	/* The link exists from the moment a start begins, so a stop can break
	 * a connect or a send in progress from any thread. */
	struct dsr_srt_link *link;
	struct dsr_ts_mux *mux;

	struct dsr_ladder_encoders *encoders;
	struct dsr_ladder_spec spec;
	bool spec_valid;
	uint64_t session;
	size_t video_count;

	bool base_set;
	int64_t offset_90k;

	uint64_t total_bytes;
	uint64_t connect_started_ns;
	int connect_time_ms;

	char *server;
	char *stream_id;
};

/* Writer side, in ladder-output-writer.c. */
void *dsr_ladder_write_thread(void *data);
bool dsr_ladder_sink_chunk(void *ctx, const uint8_t *chunk, size_t len);
void dsr_ladder_free_queue(struct dsr_ladder_output *o);
