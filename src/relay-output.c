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

/* Settings of the current profile's streaming service, or NULL. The
 * returned data is referenced; callers release it. */
static obs_data_t *current_service_settings(void)
{
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service)
		return NULL;
	return obs_service_get_settings(service);
}

static char *current_service_string(const char *field)
{
	obs_data_t *settings = current_service_settings();
	if (!settings)
		return NULL;

	const char *value = obs_data_get_string(settings, field);
	char *copy = (value && *value) ? bstrdup(value) : NULL;
	obs_data_release(settings);
	return copy;
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
	return current_service_string("server");
}

char *dsr_route_current_key(void)
{
	return current_service_string("key");
}

bool dsr_route_apply(const char *server, const char *key)
{
	if (!server || !*server || !key || !*key)
		return false;

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

int dsr_get_configured_bitrate_kbps(void)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return 0;

	const char *mode = config_get_string(config, "Output", "Mode");
	if (mode && astrcmpi(mode, "Advanced") == 0)
		return 0;

	return (int)config_get_uint(config, "SimpleOutput", "VBitrate");
}
