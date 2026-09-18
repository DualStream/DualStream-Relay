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

/* The relay as a streaming service of its own.
 *
 * A custom service in OBS carries a URL and a key and nothing else: no
 * recommended encoder settings, no codec list, and no say in which output OBS
 * builds. Registering the relay as a service type gives it all three. OBS
 * applies the keyframe interval and bitrate cap at every stream start the
 * way it does for any listed platform, refuses an encoder the relay cannot
 * take before the stream begins, and asks this service which output to run,
 * which is how a dual format stream gets the multi-rendition output without a
 * second Start button.
 *
 * It also ends the tug of war over the stream key. A connected platform
 * account writes its own key into whatever service is current at every
 * stream start. That key is not an SRT stream id, so this service leaves it
 * where it lands and keeps the relay's. */

#include <string.h>

#include <obs-module.h>
#include <plugin-support.h>

#include "ladder.h"
#include "relay-limits.h"
#include "relay-output.h"

struct dsr_service {
	char *server;
	char *key;
};

static bool srt_server(const char *value)
{
	return value && strncmp(value, "srt://", 6) == 0;
}

static bool srt_stream_id(const char *value)
{
	return value && strncmp(value, "#!::", 4) == 0;
}

static void take(char **slot, const char *value)
{
	bfree(*slot);
	*slot = bstrdup(value);
}

static const char *service_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Service.Name");
}

static void service_update(void *data, obs_data_t *settings)
{
	struct dsr_service *service = data;

	const char *server = obs_data_get_string(settings, "server");
	const char *key = obs_data_get_string(settings, "key");
	if (srt_server(server))
		take(&service->server, server);
	if (srt_stream_id(key))
		take(&service->key, key);

	/* The settings object is the one OBS saves, so what was kept is what
	 * comes back after a restart. The name is what OBS's own stream
	 * settings page shows for this service. */
	obs_data_set_string(settings, "server", service->server ? service->server : "");
	obs_data_set_string(settings, "key", service->key ? service->key : "");
	obs_data_set_string(settings, "service", DSR_SERVICE_NAME);
}

static void *service_create(obs_data_t *settings, obs_service_t *obs_service)
{
	UNUSED_PARAMETER(obs_service);
	struct dsr_service *service = bzalloc(sizeof(*service));
	service_update(service, settings);
	return service;
}

static void service_destroy(void *data)
{
	struct dsr_service *service = data;
	bfree(service->server);
	bfree(service->key);
	bfree(service);
}

static const char *service_get_url(void *data)
{
	struct dsr_service *service = data;
	return service->server;
}

static const char *service_get_key(void *data)
{
	struct dsr_service *service = data;
	return service->key;
}

static const char *service_get_connect_info(void *data, uint32_t type)
{
	struct dsr_service *service = data;
	switch (type) {
	case OBS_SERVICE_CONNECT_INFO_SERVER_URL:
		return service->server;
	case OBS_SERVICE_CONNECT_INFO_STREAM_ID:
		return service->key;
	default:
		return NULL;
	}
}

static bool service_can_try_to_connect(void *data)
{
	struct dsr_service *service = data;
	return service->server && *service->server && service->key && *service->key;
}

static const char *service_get_protocol(void *data)
{
	UNUSED_PARAMETER(data);
	return "SRT";
}

/* OBS asks this while setting a stream up, before any output exists. A
 * dual format session needs the output that carries several renditions;
 * everything else runs on OBS's own MPEG-TS output over SRT, the path every
 * relay stream has used so far. */
static const char *service_get_output_type(void *data)
{
	UNUSED_PARAMETER(data);
#ifdef DSR_LADDER_OUTPUT
	if (dsr_ladder_wanted())
		return DSR_LADDER_OUTPUT_ID;
#endif
	return NULL;
}

static const char **service_get_supported_video_codecs(void *data)
{
	UNUSED_PARAMETER(data);
	static const char *codecs[] = {"h264", NULL};
	return codecs;
}

static const char **service_get_supported_audio_codecs(void *data)
{
	UNUSED_PARAMETER(data);
	static const char *codecs[] = {"aac", NULL};
	return codecs;
}

/* Applied by OBS at every stream start unless the profile ignores service
 * recommendations. The keyframe cadence is what the relay's own gate is
 * built around; the caps keep the contribution under the rate the relay
 * refuses and at the audio rate its platforms are served at. */
static void service_apply_encoder_settings(void *data, obs_data_t *video, obs_data_t *audio)
{
	UNUSED_PARAMETER(data);
	const struct dsr_canvas_limits *limits = &dsr_limits_get()->landscape;

	if (video) {
		obs_data_set_int(video, "keyint_sec", limits->keyint_sec);
		if (obs_data_get_int(video, "bitrate") > limits->cap_kbps)
			obs_data_set_int(video, "bitrate", limits->cap_kbps);
	}
	if (audio && obs_data_get_int(audio, "bitrate") > limits->audio_kbps)
		obs_data_set_int(audio, "bitrate", limits->audio_kbps);
}

static struct obs_service_info service_info = {
	.id = DSR_SERVICE_ID,
	.get_name = service_get_name,
	.create = service_create,
	.destroy = service_destroy,
	.update = service_update,
	.get_url = service_get_url,
	.get_key = service_get_key,
	.apply_encoder_settings = service_apply_encoder_settings,
	.get_output_type = service_get_output_type,
	.get_supported_video_codecs = service_get_supported_video_codecs,
	.get_protocol = service_get_protocol,
	.get_supported_audio_codecs = service_get_supported_audio_codecs,
	.get_connect_info = service_get_connect_info,
	.can_try_to_connect = service_can_try_to_connect,
};

void dsr_service_register(void)
{
	obs_register_service(&service_info);
}
