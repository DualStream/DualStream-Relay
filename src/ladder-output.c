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

/* The stream output behind a dual format session. OBS builds it in place of
 * its own SRT output when the relay service asks for it, hands it the
 * profile's encoders, and starts it from the same button as every stream.
 *
 * Start runs on its own thread: it asks the website for the ladder, creates
 * an encoder per rendition, opens the SRT link and only then begins capture.
 * Packets are queued from the encoders and muxed by a writer thread, so a
 * slow link never holds an encoder. A failed write ends the stream as a
 * disconnect, which is what lets OBS's own reconnect logic bring it back. */

#include "ladder-output-internal.h"

#include <string.h>

#include <media-io/audio-io.h>
#include <util/platform.h>
#include <plugin-support.h>

#define ERROR_LEN 256

static const char *ladder_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Ladder.OutputName");
}

static void *ladder_create(obs_data_t *settings, obs_output_t *output)
{
	UNUSED_PARAMETER(settings);
	struct dsr_ladder_output *o = bzalloc(sizeof(*o));
	o->output = output;
	pthread_mutex_init_value(&o->queue_mutex);
	pthread_mutex_init_value(&o->start_mutex);
	if (pthread_mutex_init(&o->queue_mutex, NULL) != 0 || pthread_mutex_init(&o->start_mutex, NULL) != 0 ||
	    os_sem_init(&o->write_sem, 0) != 0 || os_event_init(&o->stop_event, OS_EVENT_TYPE_AUTO) != 0) {
		pthread_mutex_destroy(&o->queue_mutex);
		pthread_mutex_destroy(&o->start_mutex);
		os_sem_destroy(o->write_sem);
		os_event_destroy(o->stop_event);
		bfree(o);
		return NULL;
	}
	deque_init(&o->queue);
	return o;
}

/* Everything a session used on the wire. Only ever called once the writer
 * is gone: joined here, or already out after it reported a disconnect. */
static void release_transport(struct dsr_ladder_output *o)
{
	if (o->write_thread_active) {
		os_event_signal(o->stop_event);
		os_sem_post(o->write_sem);
		dsr_srt_abort(o->link);
		pthread_join(o->write_thread, NULL);
		o->write_thread_active = false;
	}
	dsr_ladder_free_queue(o);
	dsr_srt_link_destroy(o->link);
	o->link = NULL;
	dsr_ts_mux_destroy(o->mux);
	o->mux = NULL;
	o->active = false;
	dsr_ladder_report_active(0);
}

static void end_session(struct dsr_ladder_output *o)
{
	if (o->encoders) {
		dsr_ladder_detach(o->output, o->encoders);
		o->encoders = NULL;
	}
	o->spec_valid = false;
}

/* Tell a start still under way to give up and wait for it. Its HTTP call
 * polls the flag, and a connect blocked on the link returns once the link
 * is broken. A start that already finished let go of its own thread. */
static void abort_start(struct dsr_ladder_output *o)
{
	pthread_mutex_lock(&o->start_mutex);
	o->abort_start = true;
	const bool join = o->connecting;
	pthread_mutex_unlock(&o->start_mutex);

	dsr_srt_abort(o->link);
	if (join)
		pthread_join(o->start_thread, NULL);
}

/* The start thread is done with itself: a start nobody is waiting for is
 * released here, one being aborted is left for the join. */
static void finish_start(struct dsr_ladder_output *o)
{
	pthread_mutex_lock(&o->start_mutex);
	if (!o->abort_start)
		pthread_detach(o->start_thread);
	o->connecting = false;
	pthread_mutex_unlock(&o->start_mutex);
}

static void ladder_destroy(void *data)
{
	struct dsr_ladder_output *o = data;
	if (!o)
		return;

	o->stopping = true;
	abort_start(o);
	if (o->active)
		obs_output_end_data_capture(o->output);
	release_transport(o);
	end_session(o);

	pthread_mutex_destroy(&o->queue_mutex);
	pthread_mutex_destroy(&o->start_mutex);
	os_sem_destroy(o->write_sem);
	os_event_destroy(o->stop_event);
	bfree(o->server);
	bfree(o->stream_id);
	bfree(o);
}

static void fail_start(struct dsr_ladder_output *o, int code, const char *error)
{
	obs_log(LOG_WARNING, "dual format output could not start: %s", error);
	release_transport(o);
	obs_output_set_last_error(o->output, error);
	obs_output_signal_stop(o->output, code);
	finish_start(o);
}

/* The renditions this session carries. A session that OBS started for dual
 * format prepares its ladder once and keeps it across reconnects; a session
 * started for a plain stream sends OBS's own encoder and nothing else. The
 * previous session's encoders are let go of here, on a start, because that
 * is the one moment the output is certainly inactive and takes the change. */
static bool ensure_ladder(struct dsr_ladder_output *o, char *error, size_t error_len)
{
	const uint64_t session = dsr_ladder_current_session();
	if (o->spec_valid && o->session != session)
		end_session(o);

	if (!o->spec_valid) {
		end_session(o);
		if (!dsr_ladder_wanted())
			return true;
		if (!dsr_ladder_prepare(&o->spec, &o->abort_start, error, error_len))
			return false;
		o->spec_valid = true;
		o->session = session;
	}

	if (!o->encoders) {
		o->encoders = dsr_ladder_attach(o->output, &o->spec, error, error_len);
		if (!o->encoders)
			return false;
	}
	return true;
}

static bool build_mux(struct dsr_ladder_output *o, char *error, size_t error_len)
{
	o->mux = dsr_ts_mux_create(dsr_ladder_sink_chunk, o);
	o->video_count = 0;

	for (size_t i = 0; i < MAX_OUTPUT_VIDEO_ENCODERS; i++) {
		obs_encoder_t *encoder = obs_output_get_video_encoder2(o->output, i);
		if (!encoder)
			break;
		uint8_t *header = NULL;
		size_t header_size = 0;
		obs_encoder_get_extra_data(encoder, &header, &header_size);
		if (!dsr_ts_mux_add_video(o->mux, header, header_size)) {
			strncpy(error, "too many video renditions", error_len - 1);
			return false;
		}
		o->video_count++;
	}
	if (o->video_count == 0) {
		strncpy(error, "no video encoder", error_len - 1);
		return false;
	}

	obs_encoder_t *audio = obs_output_get_audio_encoder(o->output, 0);
	if (audio) {
		uint8_t *config = NULL;
		size_t config_size = 0;
		obs_encoder_get_extra_data(audio, &config, &config_size);
		audio_t *mix = obs_encoder_audio(audio);
		dsr_ts_mux_set_audio(o->mux, config, config_size, obs_encoder_get_sample_rate(audio),
				     mix ? audio_output_get_channels(mix) : 2);
	}
	return true;
}

static void *start_thread(void *data)
{
	struct dsr_ladder_output *o = data;
	char error[ERROR_LEN] = {0};

	if (!ensure_ladder(o, error, sizeof(error))) {
		fail_start(o, OBS_OUTPUT_ERROR, error);
		return NULL;
	}
	if (o->abort_start || !obs_output_can_begin_data_capture(o->output, 0) ||
	    !obs_output_initialize_encoders(o->output, 0)) {
		fail_start(o, OBS_OUTPUT_ERROR, obs_module_text("Ladder.EncodersNotReady"));
		return NULL;
	}

	if (!dsr_srt_connect(o->link, o->server, o->stream_id, error, sizeof(error))) {
		fail_start(o, OBS_OUTPUT_CONNECT_FAILED, error);
		return NULL;
	}

	if (o->abort_start || !build_mux(o, error, sizeof(error))) {
		fail_start(o, OBS_OUTPUT_ERROR, o->abort_start ? "stopped before the stream began" : error);
		return NULL;
	}

	if (pthread_create(&o->write_thread, NULL, dsr_ladder_write_thread, o) != 0) {
		fail_start(o, OBS_OUTPUT_ERROR, "writer thread could not be created");
		return NULL;
	}
	o->write_thread_active = true;
	o->connect_time_ms = (int)((os_gettime_ns() - o->connect_started_ns) / 1000000);
	o->active = true;
	obs_output_begin_data_capture(o->output, 0);
	dsr_ladder_report_active(o->video_count > 1 ? o->video_count : 0);
	obs_log(LOG_INFO, "dual format output started with %zu video rendition(s)", o->video_count);
	finish_start(o);
	return NULL;
}

static bool ladder_start(void *data)
{
	struct dsr_ladder_output *o = data;
	if (o->connecting)
		return false;

	/* A session that ended without a stop from OBS, a lost link or an
	 * encoder failure, still holds its transport; it goes before the
	 * next one is built. */
	release_transport(o);

	obs_service_t *service = obs_output_get_service(o->output);
	const char *server = service ? obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_SERVER_URL)
				     : NULL;
	const char *stream_id = service ? obs_service_get_connect_info(service, OBS_SERVICE_CONNECT_INFO_STREAM_ID)
					: NULL;
	if (!server || !*server || !stream_id || !*stream_id) {
		/* Only reachable from a reconnect, which checks nothing before
		 * calling here; the stop signal is what ends that reconnect. */
		obs_output_set_last_error(o->output, obs_module_text("Ladder.NoTarget"));
		obs_output_signal_stop(o->output, OBS_OUTPUT_BAD_PATH);
		return false;
	}
	bfree(o->server);
	bfree(o->stream_id);
	o->server = bstrdup(server);
	o->stream_id = bstrdup(stream_id);

	o->link = dsr_srt_link_create();
	if (!o->link)
		return false;

	o->stopping = false;
	o->abort_start = false;
	o->base_set = false;
	o->total_bytes = 0;
	o->connect_started_ns = os_gettime_ns();
	os_event_reset(o->stop_event);

	pthread_mutex_lock(&o->start_mutex);
	o->connecting = pthread_create(&o->start_thread, NULL, start_thread, o) == 0;
	pthread_mutex_unlock(&o->start_mutex);
	return o->connecting;
}

/* A stop from OBS. The writer is stopped and joined before the link is
 * closed, so a queued packet never turns the stop into a lost link. The
 * ladder's encoders stay attached until the next start: the output is still
 * winding its encoders down here and would refuse to let them go. */
static void ladder_stop(void *data, uint64_t ts)
{
	struct dsr_ladder_output *o = data;
	UNUSED_PARAMETER(ts);

	o->stopping = true;
	abort_start(o);

	if (o->active) {
		obs_output_end_data_capture(o->output);
		release_transport(o);
	} else {
		release_transport(o);
		obs_output_signal_stop(o->output, OBS_OUTPUT_SUCCESS);
	}
}

static void ladder_encoded_packet(void *data, struct encoder_packet *packet)
{
	struct dsr_ladder_output *o = data;

	if (!packet) {
		/* An encoder failed. The session ends here the way a lost
		 * link ends it, with the transport released so the next
		 * start finds a clean output. */
		o->stopping = true;
		obs_output_signal_stop(o->output, OBS_OUTPUT_ENCODE_ERROR);
		release_transport(o);
		return;
	}
	if (!o->active || o->stopping)
		return;

	struct dsr_queued_packet queued = {
		.data = bmemdup(packet->data, packet->size),
		.size = packet->size,
		.pts = packet->pts,
		.dts = packet->dts,
		.timebase_num = packet->timebase_num,
		.timebase_den = packet->timebase_den,
		.track = packet->track_idx,
		.video = packet->type == OBS_ENCODER_VIDEO,
		.keyframe = packet->keyframe,
	};

	pthread_mutex_lock(&o->queue_mutex);
	deque_push_back(&o->queue, &queued, sizeof(queued));
	pthread_mutex_unlock(&o->queue_mutex);
	os_sem_post(o->write_sem);
}

static uint64_t ladder_total_bytes(void *data)
{
	struct dsr_ladder_output *o = data;
	return o->total_bytes;
}

static int ladder_dropped_frames(void *data)
{
	UNUSED_PARAMETER(data);
	return 0;
}

static float ladder_congestion(void *data)
{
	UNUSED_PARAMETER(data);
	return 0.0f;
}

static int ladder_connect_time(void *data)
{
	struct dsr_ladder_output *o = data;
	return o->connect_time_ms;
}

static struct obs_output_info ladder_output_info = {
	.id = DSR_LADDER_OUTPUT_ID,
	.flags = OBS_OUTPUT_AV | OBS_OUTPUT_ENCODED | OBS_OUTPUT_MULTI_TRACK_VIDEO | OBS_OUTPUT_SERVICE,
	.get_name = ladder_get_name,
	.create = ladder_create,
	.destroy = ladder_destroy,
	.start = ladder_start,
	.stop = ladder_stop,
	.encoded_packet = ladder_encoded_packet,
	.get_total_bytes = ladder_total_bytes,
	.get_dropped_frames = ladder_dropped_frames,
	.get_congestion = ladder_congestion,
	.get_connect_time_ms = ladder_connect_time,
	.encoded_video_codecs = "h264",
	.encoded_audio_codecs = "aac",
	.protocols = "SRT",
};

void dsr_ladder_output_register(void)
{
	obs_register_output(&ladder_output_info);
}
