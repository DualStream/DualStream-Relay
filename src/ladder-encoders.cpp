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

/* The encoders behind a Twitch ladder. One per rendition, on the canvas the
 * rendition belongs to, scaled on the GPU to the rendition's size, all in
 * one encoder group so every rendition's keyframes land on the same frame.
 * They replace OBS's own stream encoder on the output for the session, in
 * ladder order, and go away with it. */

#include "ladder.h"

#include <QString>

#include <cmath>
#include <cstdio>
#include <cstring>

#include <obs-module.h>
#include <plugin-support.h>

#include "encoder-choice.hpp"
#include "vertical-canvas.hpp"

struct dsr_ladder_encoders {
	obs_encoder_group_t *group = nullptr;
	obs_encoder_t *encoders[DSR_LADDER_MAX_RENDITIONS] = {};
	size_t count = 0;
};

namespace {

/* The mix a canvas index names. Canvas 0 is OBS's program; canvas 1 is the
 * portrait canvas this plugin keeps. */
video_t *canvasVideo(int canvasIndex, obs_video_info &info)
{
	if (canvasIndex == 0)
		return obs_get_video_info(&info) ? obs_get_video() : nullptr;

	VerticalCanvas *vertical = VerticalCanvas::instance();
	if (!vertical)
		return nullptr;
	obs_canvas_t *canvas = vertical->canvasRef();
	if (!canvas)
		return nullptr;
	video_t *video = obs_canvas_get_video_info(canvas, &info) ? obs_canvas_get_video(canvas) : nullptr;
	obs_canvas_release(canvas);
	return video;
}

/* The encoder type OBS's own stream encoder uses, when it is an H.264 one,
 * so every rendition runs on the same hardware the user already picked. */
const char *encoderIdFor(obs_output_t *output)
{
	obs_encoder_t *primary = obs_output_get_video_encoder2(output, 0);
	const char *id = primary ? obs_encoder_get_id(primary) : nullptr;
	const char *codec = id ? obs_get_encoder_codec(id) : nullptr;
	if (codec && strcmp(codec, "h264") == 0)
		return id;
	return dsrPickH264EncoderId();
}

void setError(char *error, size_t errorLen, const QString &text)
{
	if (!error || errorLen == 0)
		return;
	const QByteArray utf8 = text.toUtf8();
	strncpy(error, utf8.constData(), errorLen - 1);
	error[errorLen - 1] = '\0';
}

void release(dsr_ladder_encoders *set)
{
	for (size_t i = 0; i < set->count; i++)
		obs_encoder_release(set->encoders[i]);
	if (set->group)
		obs_encoder_group_destroy(set->group);
	delete set;
}

} // namespace

struct dsr_ladder_encoders *dsr_ladder_attach(obs_output_t *output, const struct dsr_ladder_spec *spec, char *error,
					      size_t error_len)
{
	const char *encoderId = encoderIdFor(output);
	dsr_ladder_encoders *set = new dsr_ladder_encoders;
	set->group = obs_encoder_group_create();

	for (size_t i = 0; i < spec->count; i++) {
		const dsr_ladder_rendition &rendition = spec->renditions[i];

		obs_video_info info;
		video_t *video = canvasVideo(rendition.canvas_index, info);
		if (!video) {
			setError(error, error_len, QString::fromUtf8(obs_module_text("Ladder.CanvasMissing")));
			release(set);
			return nullptr;
		}

		/* Constant bitrate at the dictated figure, the keyframe cadence
		 * Twitch asked for, and parameter sets on every keyframe so a
		 * rendition can be joined at any of them. The rest follows the
		 * encoder's own defaults, as it does for the stream encoder. */
		obs_data_t *settings = obs_data_create();
		obs_data_set_int(settings, "bitrate", rendition.bitrate_kbps);
		obs_data_set_int(settings, "keyint_sec", rendition.keyint_sec);
		obs_data_set_string(settings, "rate_control", "CBR");
		obs_data_set_string(settings, "profile", "high");
		obs_data_set_bool(settings, "repeat_headers", true);

		char name[32];
		snprintf(name, sizeof(name), "dsr_ladder_%zu", i);
		obs_encoder_t *encoder = obs_video_encoder_create(encoderId, name, settings, nullptr);
		obs_data_release(settings);
		if (!encoder) {
			setError(error, error_len,
				 QString::fromUtf8(obs_module_text("Ladder.EncoderFailed")).arg(encoderId));
			release(set);
			return nullptr;
		}
		set->encoders[set->count++] = encoder;

		obs_encoder_set_video(encoder, video);
		if (rendition.width != info.output_width || rendition.height != info.output_height) {
			obs_encoder_set_scaled_size(encoder, rendition.width, rendition.height);
			obs_encoder_set_gpu_scale_type(encoder, OBS_SCALE_BICUBIC);
		}

		/* A rendition at a lower frame rate takes every Nth frame when
		 * the canvas rate is a whole multiple of it. */
		const double canvasFps = info.fps_den ? (double)info.fps_num / (double)info.fps_den : 0.0;
		const double renditionFps = rendition.fps_den ? (double)rendition.fps_num / (double)rendition.fps_den
							      : 0.0;
		if (canvasFps > 0.0 && renditionFps > 0.0 && canvasFps > renditionFps) {
			const double ratio = canvasFps / renditionFps;
			const uint32_t divisor = (uint32_t)std::llround(ratio);
			if (divisor > 1 && std::fabs(ratio - (double)divisor) < 0.01)
				obs_encoder_set_frame_rate_divisor(encoder, divisor);
		}

		if (!obs_encoder_set_group(encoder, set->group)) {
			setError(error, error_len, QString::fromUtf8(obs_module_text("Ladder.GroupFailed")));
			release(set);
			return nullptr;
		}
	}

	for (size_t i = 0; i < set->count; i++)
		obs_output_set_video_encoder2(output, set->encoders[i], i);

	obs_log(LOG_INFO, "dual format ladder attached: %zu renditions on %s", set->count, encoderId);
	return set;
}

/* Runs on a start, while the output is inactive, so the slots take the
 * change. Slot 0 is left alone: OBS put its own stream encoder back there
 * when it set the stream up, and the next attach replaces it again. Every
 * slot above it is cleared, not only the ones this ladder used, so a shorter
 * ladder can never leave an earlier one's rendition behind. */
void dsr_ladder_detach(obs_output_t *output, struct dsr_ladder_encoders *set)
{
	if (!set)
		return;
	for (size_t i = 1; i < MAX_OUTPUT_VIDEO_ENCODERS; i++)
		obs_output_set_video_encoder2(output, nullptr, i);
	release(set);
}
