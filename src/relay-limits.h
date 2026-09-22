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

/* What the relay carries per canvas and the encoder settings that keep a
 * contribution inside its gates. Every figure the plugin shows or applies
 * comes from here, so the relay's limits are stated in one place. */
struct dsr_canvas_limits {
	uint32_t max_width;
	uint32_t max_height;
	double max_fps;

	/* The video bitrate the relay is tuned for. Above it the relay's
	 * platforms gain nothing and the upload only grows. */
	int video_kbps;

	/* The most the relay's platforms are ever served at, which is where
	 * the relay service caps an encoder set higher at stream start. */
	int cap_kbps;

	/* The video bitrate the relay refuses. Its rate gate trips at one and
	 * a half times the tier it resolves for the account, and a stream
	 * that trips it twice is re-encoded on the relay for the rest of the
	 * session. The plugin cannot see which tier an account resolves to,
	 * so this is the figure every account clears. */
	int unfit_kbps;

	/* Audio bitrate the relay's platforms are served at. */
	int audio_kbps;

	/* Keyframe cadence the relay is built around. */
	int keyint_sec;

	/* The longest keyframe interval that stays clear of the relay's
	 * keyframe gate, which ends passthrough after 8.5 seconds of video
	 * with no keyframe. */
	int keyint_max_sec;
};

struct dsr_relay_limits {
	struct dsr_canvas_limits landscape;
	struct dsr_canvas_limits portrait;
};

/* The figures in force: the relay's, once it has handed them over with the
 * ingest target, else the plugin's own. */
const struct dsr_relay_limits *dsr_limits_get(void);

/* The plugin's own figures, the ones every account clears. */
const struct dsr_relay_limits *dsr_limits_defaults(void);

/* Take the figures the relay resolved for this account; everything that
 * reads them sees the new ones from the moment this returns. */
void dsr_limits_apply(const struct dsr_relay_limits *resolved);
bool dsr_limits_from_relay(void);

#ifdef __cplusplus
}
#endif
