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

/* The portrait publish. Split from the canvas manager because scene
 * management and encoding share nothing but the canvas pointer. */

#include "vertical-canvas.hpp"

#include <QMetaObject>

#include <cstring>

#include <obs-module.h>
#include <plugin-support.h>

#include "relay-output.h"

namespace {

const int kPortraitAudioKbps = 160;
const int kReconnectRetries = 25;
const int kReconnectDelaySec = 2;

/* Hardware first, matching what simple output mode would pick on the same
 * machine, with obs_x264 as the floor. H.264 only, the relay's contract. */
const char *pickEncoderId()
{
	static const char *preferred[] = {"obs_nvenc_h264_tex", "ffmpeg_nvenc", "obs_qsv11_v2", "h264_texture_amf"};

	for (const char *candidate : preferred) {
		const char *id = nullptr;
		for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
			if (id && strcmp(id, candidate) == 0)
				return candidate;
		}
	}
	return "obs_x264";
}

} // namespace

void VerticalCanvas::onOutputStopped(void *data, calldata_t *)
{
	VerticalCanvas *self = static_cast<VerticalCanvas *>(data);
	QMetaObject::invokeMethod(self, [self]() { self->releaseOutput(); }, Qt::QueuedConnection);
}

/* Start the publish once everything it needs is true: the canvas exists, OBS
 * is streaming, the landscape output points at the relay, an enabled
 * destination takes the portrait canvas, and the target is known. Called from
 * every place one of those can flip, so a destination enabled mid-stream
 * still brings the portrait feed up. */
void VerticalCanvas::maybeStartOutput()
{
	if (!canvas || publishing())
		return;
	if (portraitServer.isEmpty() || portraitKey.isEmpty() || !hasPortraitDests)
		return;
	if (!obs_frontend_streaming_active() || !dsr_route_is_relay())
		return;

	ensureVideo();
	video_t *video = obs_canvas_get_video(canvas);
	if (!video) {
		obs_log(LOG_WARNING, "portrait output skipped: canvas has no video mix");
		return;
	}

	releaseOutput();

	obs_data_t *serviceSettings = obs_data_create();
	obs_data_set_string(serviceSettings, "server", portraitServer.toUtf8().constData());
	obs_data_set_string(serviceSettings, "key", portraitKey.toUtf8().constData());
	obs_data_set_bool(serviceSettings, "use_auth", false);
	service = obs_service_create_private("rtmp_custom", "dsr_vertical_service", serviceSettings);
	obs_data_release(serviceSettings);

	/* Portrait is the same pixel count rotated, and the desktop app
	 * publishes its vertical feed at the horizontal bitrate by default.
	 * The service adds the SRT essentials, repeated headers and ADTS. */
	obs_data_t *videoSettings = obs_data_create();
	obs_data_set_int(videoSettings, "bitrate", DSR_TARGET_BITRATE_KBPS);
	obs_data_set_int(videoSettings, "keyint_sec", DSR_TARGET_KEYINT_SEC);

	obs_data_t *audioSettings = obs_data_create();
	obs_data_set_int(audioSettings, "bitrate", kPortraitAudioKbps);

	obs_service_apply_encoder_settings(service, videoSettings, audioSettings);

	const char *encoderId = pickEncoderId();
	videoEncoder = obs_video_encoder_create(encoderId, "dsr_vertical_video", videoSettings, nullptr);
	audioEncoder = obs_audio_encoder_create("ffmpeg_aac", "dsr_vertical_audio", audioSettings, 0, nullptr);
	obs_data_release(videoSettings);
	obs_data_release(audioSettings);

	if (!videoEncoder || !audioEncoder) {
		obs_log(LOG_ERROR, "portrait encoders could not be created (%s)", encoderId);
		releaseOutput();
		return;
	}

	obs_encoder_set_video(videoEncoder, video);
	obs_encoder_set_audio(audioEncoder, obs_get_audio());

	output = obs_output_create("ffmpeg_mpegts_muxer", "dsr_vertical_output", nullptr, nullptr);
	if (!output) {
		obs_log(LOG_ERROR, "portrait output could not be created");
		releaseOutput();
		return;
	}

	obs_output_set_video_encoder(output, videoEncoder);
	obs_output_set_audio_encoder(output, audioEncoder, 0);
	obs_output_set_service(output, service);
	/* Mirrors the frontend's own defaults; reconnecting is what lets a
	 * dropped uplink resume into the relay's protection window. */
	obs_output_set_reconnect_settings(output, kReconnectRetries, kReconnectDelaySec);
	signal_handler_connect(obs_output_get_signal_handler(output), "stop", onOutputStopped, this);

	if (!obs_output_start(output)) {
		obs_log(LOG_WARNING, "portrait output failed to start: %s", obs_output_get_last_error(output));
		releaseOutput();
		return;
	}

	obs_log(LOG_INFO, "portrait output started (%s, %d kbps)", encoderId, DSR_TARGET_BITRATE_KBPS);
}

void VerticalCanvas::stopOutput(bool force)
{
	if (!output)
		return;
	if (force)
		obs_output_force_stop(output);
	else if (obs_output_active(output))
		obs_output_stop(output);
}

void VerticalCanvas::releaseOutput()
{
	if (output && obs_output_active(output))
		obs_output_force_stop(output);

	if (output) {
		signal_handler_disconnect(obs_output_get_signal_handler(output), "stop", onOutputStopped, this);
		obs_output_release(output);
		output = nullptr;
	}
	if (videoEncoder) {
		obs_encoder_release(videoEncoder);
		videoEncoder = nullptr;
	}
	if (audioEncoder) {
		obs_encoder_release(audioEncoder);
		audioEncoder = nullptr;
	}
	if (service) {
		obs_service_release(service);
		service = nullptr;
	}
}
