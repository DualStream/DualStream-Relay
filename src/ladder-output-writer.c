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

/* The writer thread of the dual format output: takes queued packets in the
 * order the encoders delivered them, restates their timestamps on one clock,
 * and hands them to the transport stream writer, whose chunks go straight
 * onto the SRT link. */

#include "ladder-output-internal.h"

#include <plugin-support.h>

/* Where the first decode time lands, in 90 kHz ticks: ten seconds, so the
 * clock reference that runs behind it never goes negative. */
#define TIMESTAMP_BASE_90K 900000

void dsr_ladder_free_queue(struct dsr_ladder_output *o)
{
	pthread_mutex_lock(&o->queue_mutex);
	while (o->queue.size >= sizeof(struct dsr_queued_packet)) {
		struct dsr_queued_packet packet;
		deque_pop_front(&o->queue, &packet, sizeof(packet));
		bfree(packet.data);
	}
	deque_free(&o->queue);
	deque_init(&o->queue);
	pthread_mutex_unlock(&o->queue_mutex);
}

/* What actually went on the wire, headers and padding included, which is
 * the figure OBS turns into the bitrate on its status bar. */
bool dsr_ladder_sink_chunk(void *ctx, const uint8_t *chunk, size_t len)
{
	struct dsr_ladder_output *o = ctx;
	if (!dsr_srt_send(o->link, chunk, len))
		return false;
	o->total_bytes += len;
	return true;
}

static int64_t to_90k(int64_t value, int32_t num, int32_t den)
{
	if (den <= 0)
		return 0;
	return value * 90000 * num / den;
}

static bool mux_packet(struct dsr_ladder_output *o, const struct dsr_queued_packet *packet)
{
	/* Every stream starts near zero; the first decode time seen fixes
	 * one offset for all of them. */
	if (!o->base_set) {
		o->offset_90k = TIMESTAMP_BASE_90K - to_90k(packet->dts, packet->timebase_num, packet->timebase_den);
		o->base_set = true;
	}
	const int64_t pts = to_90k(packet->pts, packet->timebase_num, packet->timebase_den) + o->offset_90k;
	const int64_t dts = to_90k(packet->dts, packet->timebase_num, packet->timebase_den) + o->offset_90k;

	if (packet->video)
		return dsr_ts_mux_write_video(o->mux, packet->track, packet->data, packet->size, pts, dts,
					      packet->keyframe);
	return dsr_ts_mux_write_audio(o->mux, packet->data, packet->size, pts);
}

void *dsr_ladder_write_thread(void *data)
{
	struct dsr_ladder_output *o = data;

	while (os_sem_wait(o->write_sem) == 0) {
		if (os_event_try(o->stop_event) == 0)
			break;

		struct dsr_queued_packet packet;
		bool have = false;
		pthread_mutex_lock(&o->queue_mutex);
		if (o->queue.size >= sizeof(packet)) {
			deque_pop_front(&o->queue, &packet, sizeof(packet));
			have = true;
		}
		pthread_mutex_unlock(&o->queue_mutex);
		if (!have)
			continue;

		const bool ok = mux_packet(o, &packet);
		bfree(packet.data);
		if (ok)
			continue;

		/* A send that fails because a stop broke the link is the stop
		 * doing its job. Any other failure is a lost link, reported as
		 * a disconnect so OBS retries. Either way this thread only
		 * leaves; the lifecycle side joins it and frees what it used. */
		if (!o->stopping) {
			obs_log(LOG_WARNING, "dual format output lost the relay link");
			o->active = false;
			obs_output_signal_stop(o->output, OBS_OUTPUT_DISCONNECTED);
		}
		break;
	}
	return NULL;
}
