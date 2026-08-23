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

/* Reading the profile: output stats, video settings, the streaming encoder's
 * configuration, and the connected-account state. Routing lives in
 * relay-route.c. */

#include <string.h>

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <plugin-support.h>

#include "relay-output.h"

void dsr_get_local_stats(struct dsr_local_stats *stats)
{
	memset(stats, 0, sizeof(*stats));

	obs_output_t *output = obs_frontend_get_streaming_output();
	if (!output)
		return;

	stats->active = obs_output_active(output);
	stats->total_frames = obs_output_get_total_frames(output);
	stats->dropped_frames = obs_output_get_frames_dropped(output);
	stats->congestion = obs_output_get_congestion(output);
	obs_output_release(output);
}

bool dsr_get_video_summary(struct dsr_video_summary *summary)
{
	struct obs_video_info ovi;

	memset(summary, 0, sizeof(*summary));
	if (!obs_get_video_info(&ovi))
		return false;

	summary->output_width = ovi.output_width;
	summary->output_height = ovi.output_height;
	if (ovi.fps_den > 0)
		summary->fps = (double)ovi.fps_num / (double)ovi.fps_den;
	return true;
}

char *dsr_get_connected_account(void)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return NULL;

	/* OBS records the connected account in the profile config as
	 * [Auth] Type=<service>. It applies that account's stream key to the
	 * service when streaming starts. */
	const char *type = config_get_string(config, "Auth", "Type");
	return (type && *type) ? bstrdup(type) : NULL;
}

static bool profile_is_advanced(config_t *config)
{
	const char *mode = config_get_string(config, "Output", "Mode");
	return mode && astrcmpi(mode, "Advanced") == 0;
}

/* Advanced output mode keeps the streaming encoder's settings in this file,
 * beside the profile, and reloads it at the start of every stream. */
static char *stream_encoder_path(void)
{
	char *profile = obs_frontend_get_current_profile_path();
	if (!profile)
		return NULL;

	struct dstr path = {0};
	dstr_copy(&path, profile);
	dstr_cat(&path, "/streamEncoder.json");
	bfree(profile);
	return path.array;
}

bool dsr_encoder_read(struct dsr_encoder_settings *out)
{
	memset(out, 0, sizeof(*out));
	out->keyint_sec = -1;

	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return false;

	out->advanced = profile_is_advanced(config);
	if (!out->advanced) {
		out->video_bitrate_kbps = (int)config_get_uint(config, "SimpleOutput", "VBitrate");
		return true;
	}

	char *path = stream_encoder_path();
	if (!path)
		return false;

	/* No file means the profile has never had its encoder settings edited
	 * and OBS is running the encoder's own defaults. Both figures stay
	 * unknown, which reads correctly: there is nothing here to compare. */
	obs_data_t *settings = obs_data_create_from_json_file(path);
	bfree(path);
	if (!settings)
		return true;

	if (obs_data_has_user_value(settings, "bitrate"))
		out->video_bitrate_kbps = (int)obs_data_get_int(settings, "bitrate");
	if (obs_data_has_user_value(settings, "keyint_sec"))
		out->keyint_sec = (int)obs_data_get_int(settings, "keyint_sec");
	obs_data_release(settings);
	return true;
}

char *dsr_get_stream_video_codec(void)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return NULL;

	if (profile_is_advanced(config)) {
		const char *encoder = config_get_string(config, "AdvOut", "Encoder");
		if (!encoder || !*encoder)
			return NULL;

		const char *codec = obs_get_encoder_codec(encoder);
		return (codec && *codec) ? bstrdup(codec) : NULL;
	}

	/* Simple mode stores one of OBS's own shorthand names rather than an
	 * encoder id. The shorthand carries the codec in its suffix
	 * ("nvenc_hevc", "amd_av1"), and that spelling has stayed stable
	 * across releases in a way the shorthand-to-encoder-id table has not,
	 * so the suffix is what gets read. No suffix means H.264. */
	const char *simple = config_get_string(config, "SimpleOutput", "StreamEncoder");
	if (!simple || !*simple)
		return NULL;
	if (strstr(simple, "hevc"))
		return bstrdup("hevc");
	if (strstr(simple, "av1"))
		return bstrdup("av1");
	return bstrdup("h264");
}

char *dsr_get_stream_audio_codec(void)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return NULL;

	if (profile_is_advanced(config)) {
		const char *encoder = config_get_string(config, "AdvOut", "AudioEncoder");
		if (!encoder || !*encoder)
			return NULL;

		const char *codec = obs_get_encoder_codec(encoder);
		return (codec && *codec) ? bstrdup(codec) : NULL;
	}

	/* Simple mode stores the codec name itself: "aac" or "opus". */
	const char *simple = config_get_string(config, "SimpleOutput", "StreamAudioEncoder");
	return (simple && *simple) ? bstrdup(simple) : NULL;
}

char *dsr_encoder_rate_control(void)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return NULL;

	/* Simple mode builds its streaming encoder as CBR every time; there is
	 * no stored choice to read. */
	if (!profile_is_advanced(config))
		return bstrdup("CBR");

	char *path = stream_encoder_path();
	if (!path)
		return NULL;

	obs_data_t *settings = obs_data_create_from_json_file(path);
	bfree(path);
	if (!settings)
		return NULL;

	char *result = NULL;
	if (obs_data_has_user_value(settings, "rate_control")) {
		const char *value = obs_data_get_string(settings, "rate_control");
		if (value && *value)
			result = bstrdup(value);
	}
	obs_data_release(settings);
	return result;
}

bool dsr_encoder_write(int video_bitrate_kbps, int keyint_sec)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config || video_bitrate_kbps <= 0)
		return false;

	if (!profile_is_advanced(config)) {
		/* Simple mode builds the encoder from the profile config every
		 * time it starts a stream, so this takes effect at the next
		 * Start Streaming. It offers no keyframe-interval setting, so
		 * keyint_sec has nowhere to go here and is left to the encoder. */
		config_set_uint(config, "SimpleOutput", "VBitrate", (uint64_t)video_bitrate_kbps);
		config_save_safe(config, "tmp", NULL);
		obs_log(LOG_INFO, "simple output video bitrate set to %d kbps", video_bitrate_kbps);
		return true;
	}

	char *path = stream_encoder_path();
	if (!path)
		return false;

	obs_data_t *settings = obs_data_create_from_json_file(path);
	if (!settings)
		settings = obs_data_create();

	obs_data_set_int(settings, "bitrate", video_bitrate_kbps);
	if (keyint_sec >= 0)
		obs_data_set_int(settings, "keyint_sec", keyint_sec);

	const bool saved = obs_data_save_json_safe(settings, path, "tmp", "bak");
	obs_data_release(settings);
	bfree(path);

	if (saved)
		obs_log(LOG_INFO, "stream encoder set to %d kbps, %d second keyframe interval", video_bitrate_kbps,
			keyint_sec);
	return saved;
}

int dsr_get_configured_bitrate_kbps(void)
{
	struct dsr_encoder_settings settings;

	if (!dsr_encoder_read(&settings))
		return 0;
	return settings.video_bitrate_kbps;
}
