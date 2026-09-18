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
#include <plugin-support.h>

#include "relay-output.h"
#include "relay-profile.h"

static const char *kStreamSection = "Stream1";
static const char *kMultitrackKey = "EnableMultitrackVideo";
static const char *kIgnoreRecommendedKey = "IgnoreRecommended";
static const char *kOutputSection = "Output";
static const char *kOutputModeKey = "Mode";
static const char *kAdvancedSection = "AdvOut";
static const char *kApplyServiceSettingsKey = "ApplyServiceSettings";

static bool restart_pending;

bool dsr_profile_clear_enhanced_broadcasting(bool after_launch)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return false;

	/* An explicit "off" is left alone: only "on" builds the machinery. */
	if (!config_has_user_value(config, kStreamSection, kMultitrackKey) ||
	    !config_get_bool(config, kStreamSection, kMultitrackKey))
		return false;

	config_remove_value(config, kStreamSection, kMultitrackKey);
	config_save_safe(config, "tmp", NULL);

	if (after_launch)
		restart_pending = true;

	obs_log(LOG_INFO, "Enhanced Broadcasting was on in this profile; turned off for the relay%s",
		after_launch ? " (takes effect after a restart)" : "");
	return true;
}

bool dsr_profile_restart_pending(void)
{
	return restart_pending;
}

bool dsr_profile_service_names_relay(void)
{
	char *profile = obs_frontend_get_current_profile_path();
	if (!profile)
		return false;

	struct dstr path = {0};
	dstr_copy(&path, profile);
	dstr_cat(&path, "/service.json");
	bfree(profile);

	obs_data_t *file = obs_data_create_from_json_file(path.array);
	dstr_free(&path);
	if (!file)
		return false;

	bool relay = false;
	const char *type = obs_data_get_string(file, "type");
	if (type && strcmp(type, DSR_SERVICE_ID) == 0) {
		relay = true;
	} else {
		obs_data_t *settings = obs_data_get_obj(file, "settings");
		const char *server = settings ? obs_data_get_string(settings, "server") : NULL;
		relay = server && strstr(server, DSR_INGEST_HOST) != NULL;
		obs_data_release(settings);
	}

	obs_data_release(file);
	return relay;
}

bool dsr_profile_applies_service_settings(void)
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return false;
	const char *mode = config_get_string(config, kOutputSection, kOutputModeKey);
	if (!mode || astrcmpi(mode, "Advanced") != 0)
		return true;
	return config_get_bool(config, kAdvancedSection, kApplyServiceSettingsKey);
}

bool dsr_profile_ignores_recommended(void)
{
	config_t *config = obs_frontend_get_profile_config();
	return config && config_get_bool(config, kStreamSection, kIgnoreRecommendedKey);
}

void dsr_profile_restart_satisfied(void)
{
	restart_pending = false;
}
