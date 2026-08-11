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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host substring that identifies a relay ingest URL. The authoritative
 * server URL comes from the ingest-target API response; this is only used
 * to recognize whether the current profile already points at the relay. */
#define DSR_INGEST_HOST "ingest.dualstream.gg"

/* The plugin never owns an output. It points the profile's streaming
 * service at the relay (with the user's consent) and restores the previous
 * service on request. A snapshot of the replaced service is kept in the
 * module config directory so routing is always reversible. */

bool dsr_route_is_relay(void);

/* Returned strings are allocated with bstrdup(); callers bfree() them.
 * NULL when there is no streaming service or no such field. */
char *dsr_route_current_server(void);
char *dsr_route_current_key(void);

bool dsr_route_apply(const char *server, const char *key);
bool dsr_route_restore(void);
bool dsr_route_have_snapshot(void);

struct dsr_local_stats {
	bool active;
	int total_frames;
	int dropped_frames;
	float congestion;
};

void dsr_get_local_stats(struct dsr_local_stats *stats);

struct dsr_video_summary {
	uint32_t output_width;
	uint32_t output_height;
	double fps;
};

bool dsr_get_video_summary(struct dsr_video_summary *summary);

/* Simple-output video bitrate in kbps. Returns 0 when the profile uses
 * advanced output mode, where the encoder settings are not readable from
 * the profile config; the preflight note is simply omitted then. */
int dsr_get_configured_bitrate_kbps(void);

#ifdef __cplusplus
}
#endif
