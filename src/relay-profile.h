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

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Profile settings that decide whether a relay stream can start at all.
 *
 * OBS builds its Enhanced Broadcasting machinery at launch whenever the
 * profile has that option on, whatever service the profile points at. The
 * relay service carries no configuration endpoint for it, so with the option
 * left on from an earlier Twitch setup every Start Streaming press fails with
 * a "missing config URL" dialog before the relay is ever contacted. OBS only
 * clears the option when its own stream settings page is saved, which a
 * profile routed by this plugin never goes through. */

/* Remove the option from the profile when it is on. Returns true when a value
 * was removed. after_launch says whether OBS has already built its outputs
 * for this profile: a removal at module load takes effect at once, while one
 * made later needs a restart, and the dock says so. */
bool dsr_profile_clear_enhanced_broadcasting(bool after_launch);

/* True once a removal happened after OBS built its outputs, until restart. */
bool dsr_profile_restart_pending(void);

/* Whether the profile on disk points its stream output at the relay. Read
 * from the profile's own service file, for the one moment the streaming
 * service is not loaded yet: module load. */
bool dsr_profile_service_names_relay(void);

/* Whether OBS lets the streaming service touch the encoder at stream start.
 * Simple output mode always applies the service's settings; advanced mode
 * only with "Enforce streaming service encoder settings" on. */
bool dsr_profile_applies_service_settings(void);

/* The "Ignore streaming service setting recommendations" switch on the
 * stream page. It puts the profile's own bitrate back after the service has
 * applied its settings; the service's keyframe interval stays. */
bool dsr_profile_ignores_recommended(void);

/* A removal happened and the profile's outputs have been rebuilt since, so
 * the restart note no longer applies. */
void dsr_profile_restart_satisfied(void);

#ifdef __cplusplus
}
#endif
