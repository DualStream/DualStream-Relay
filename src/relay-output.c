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

#include <string.h>

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <plugin-support.h>

#include "relay-output.h"

static char *snapshot_path(void)
{
	return obs_module_config_path("previous-service.json");
}

static void ensure_config_dir(void)
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
}

/* Ask the service what it will actually connect with, rather than reading
 * raw settings: rtmp_common (a services.json entry) keeps a service name and
 * resolves the URL from it, while rtmp_custom stores the URL directly. Only
 * connect info is correct for both. */
static char *current_connect_info(enum obs_service_connect_info type)
{
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service)
		return NULL;

	const char *value = obs_service_get_connect_info(service, type);
	return (value && *value) ? bstrdup(value) : NULL;
}

bool dsr_route_is_relay(void)
{
	char *server = dsr_route_current_server();
	bool relay = server && strstr(server, DSR_INGEST_HOST) != NULL;
	bfree(server);
	return relay;
}

char *dsr_route_current_server(void)
{
	return current_connect_info(OBS_SERVICE_CONNECT_INFO_SERVER_URL);
}

char *dsr_route_current_key(void)
{
	return current_connect_info(OBS_SERVICE_CONNECT_INFO_STREAM_KEY);
}

bool dsr_route_is_service_entry(void)
{
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service || strcmp(obs_service_get_type(service), "rtmp_common") != 0)
		return false;

	obs_data_t *settings = obs_service_get_settings(service);
	const char *name = obs_data_get_string(settings, "service");
	const bool ours = name && strcmp(name, DSR_SERVICE_NAME) == 0;
	obs_data_release(settings);
	return ours;
}

#define SRT_SCHEME "srt://"

static bool is_srt_url(const char *url)
{
	return url && strncmp(url, SRT_SCHEME, sizeof(SRT_SCHEME) - 1) == 0;
}

static int hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Percent-decode into a growable string. The relay percent-encodes the
 * streamid when it mints the URL, and libsrt wants the literal
 * "#!::r=relay/<key>_landscape,m=publish" form. An escape that does not
 * decode is passed through as written rather than dropped. */
static void append_decoded(struct dstr *out, const char *value, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		if (value[i] == '%' && i + 2 < len) {
			const int hi = hex_value(value[i + 1]);
			const int lo = hex_value(value[i + 2]);
			if (hi >= 0 && lo >= 0) {
				const char decoded = (char)((hi << 4) | lo);
				dstr_ncat(out, &decoded, 1);
				i += 2;
				continue;
			}
		}
		dstr_ncat(out, value + i, 1);
	}
}

bool dsr_srt_prepare(const char *relay_url, char **server_out, char **stream_id_out)
{
	if (!is_srt_url(relay_url) || !server_out || !stream_id_out)
		return false;

	const char *query = strchr(relay_url, '?');
	if (!query)
		return false;

	struct dstr server = {0};
	struct dstr stream_id = {0};
	dstr_ncat(&server, relay_url, (size_t)(query - relay_url));

	bool first = true;
	for (const char *segment = query + 1; *segment;) {
		const char *end = strchr(segment, '&');
		const size_t len = end ? (size_t)(end - segment) : strlen(segment);
		const char *equals = memchr(segment, '=', len);
		const size_t name_len = equals ? (size_t)(equals - segment) : len;

		/* The streamid moves out of the URL and into the Stream Key
		 * field. It has to: a streamid left in the query overrides
		 * whatever the key field holds, and it opens with a '#', which
		 * anything parsing the URL properly reads as the start of a
		 * fragment. Latency is dropped here and rewritten below.
		 * Everything else the relay minted is kept verbatim, in order. */
		const bool is_streamid = name_len == 8 && astrcmpi_n(segment, "streamid", 8) == 0;
		const bool is_latency = name_len == 7 && astrcmpi_n(segment, "latency", 7) == 0;

		if (is_streamid && equals) {
			append_decoded(&stream_id, equals + 1, len - name_len - 1);
		} else if (!is_streamid && !is_latency) {
			dstr_cat(&server, first ? "?" : "&");
			dstr_ncat(&server, segment, len);
			first = false;
		}

		if (!end)
			break;
		segment = end + 1;
	}

	if (dstr_is_empty(&stream_id)) {
		dstr_free(&server);
		dstr_free(&stream_id);
		return false;
	}

	/* OBS reads both latency figures out of the query in microseconds and
	 * divides by a thousand before handing them to libsrt, which takes
	 * milliseconds. The relay writes them in milliseconds, the way every
	 * other SRT implementation reads them, so the numbers have to be
	 * restated here or the link comes up with a four millisecond window and
	 * behaves worse than one with no window at all.
	 *
	 * peerlatency is the figure that matters to a publisher. latency on its
	 * own sizes the local receive buffer, which a sender never fills;
	 * peerlatency is the one the listener negotiates up to, so it is what
	 * actually decides how long the relay will wait for a retransmission. */
	dstr_catf(&server, "%slatency=%d&peerlatency=%d", first ? "?" : "&", DSR_SRT_LATENCY_MS * 1000,
		  DSR_SRT_LATENCY_MS * 1000);

	*server_out = server.array;
	*stream_id_out = stream_id.array;
	return true;
}

bool dsr_route_apply(const char *server, const char *key)
{
	if (!server || !*server || !key || !*key)
		return false;

	/* Already on the services.json entry: keep the user there and only
	 * set the key. Replacing it with a custom service would silently undo
	 * their choice of service and lose the recommended settings. An SRT
	 * target skips this, because that entry can only carry RTMPS. */
	if (!is_srt_url(server) && dsr_route_is_service_entry()) {
		obs_service_t *current = obs_frontend_get_streaming_service();
		obs_data_t *update = obs_service_get_settings(current);
		obs_data_set_string(update, "key", key);
		obs_service_update(current, update);
		obs_data_release(update);
		obs_frontend_save_streaming_service();
		obs_log(LOG_INFO, "stream key set on the %s service entry", DSR_SERVICE_NAME);
		return true;
	}

	/* Snapshot whatever the profile pointed at before, so "Restore
	 * previous settings" always has something to go back to. Re-routing
	 * while already on the relay must not overwrite that snapshot. */
	obs_service_t *current = obs_frontend_get_streaming_service();
	if (current && !dsr_route_is_relay()) {
		obs_data_t *settings = obs_service_get_settings(current);
		obs_data_t *snapshot = obs_data_create();
		obs_data_set_string(snapshot, "type", obs_service_get_type(current));
		obs_data_set_string(snapshot, "settings", obs_data_get_json(settings));

		ensure_config_dir();
		char *path = snapshot_path();
		if (path) {
			obs_data_save_json_safe(snapshot, path, "tmp", "bak");
			bfree(path);
		}
		obs_data_release(snapshot);
		obs_data_release(settings);
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "server", server);
	obs_data_set_string(settings, "key", key);
	obs_data_set_bool(settings, "use_auth", false);

	obs_service_t *service = obs_service_create("rtmp_custom", "default_service", settings, NULL);
	obs_data_release(settings);
	if (!service)
		return false;

	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	obs_service_release(service);

	obs_log(LOG_INFO, "stream output routed to the relay");
	return true;
}

bool dsr_route_have_snapshot(void)
{
	char *path = snapshot_path();
	if (!path)
		return false;
	bool exists = os_file_exists(path);
	bfree(path);
	return exists;
}

bool dsr_route_restore(void)
{
	char *path = snapshot_path();
	if (!path)
		return false;

	obs_data_t *snapshot = obs_data_create_from_json_file(path);
	if (!snapshot) {
		bfree(path);
		return false;
	}

	const char *type = obs_data_get_string(snapshot, "type");
	const char *json = obs_data_get_string(snapshot, "settings");
	obs_data_t *settings = obs_data_create_from_json(json);

	obs_service_t *service =
		obs_service_create((type && *type) ? type : "rtmp_common", "default_service", settings, NULL);
	obs_data_release(settings);
	obs_data_release(snapshot);

	if (!service) {
		bfree(path);
		return false;
	}

	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	obs_service_release(service);

	os_unlink(path);
	bfree(path);

	obs_log(LOG_INFO, "stream output restored to the previous service");
	return true;
}

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
	if (!config || !profile_is_advanced(config))
		return NULL;

	const char *encoder = config_get_string(config, "AdvOut", "Encoder");
	if (!encoder || !*encoder)
		return NULL;

	const char *codec = obs_get_encoder_codec(encoder);
	return (codec && *codec) ? bstrdup(codec) : NULL;
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
