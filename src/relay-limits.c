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

#include "relay-limits.h"

/* The relay resolves a bitrate tier per account when a stream starts, and a
 * partner's tier sits above these figures. The plugin cannot see partner
 * status, so it offers the settings every account clears and keeps the
 * refusal threshold at the lowest tier the relay resolves. */
static const struct dsr_relay_limits limits = {
	.landscape =
		{
			.max_width = 1920,
			.max_height = 1080,
			.max_fps = 60.0,
			.video_kbps = 6000,
			.cap_kbps = 7500,
			.unfit_kbps = 8100,
			.audio_kbps = 160,
			.keyint_sec = 2,
			.keyint_max_sec = 4,
		},
	.portrait =
		{
			.max_width = 1080,
			.max_height = 1920,
			.max_fps = 60.0,
			.video_kbps = 6000,
			.cap_kbps = 7500,
			.unfit_kbps = 8100,
			.audio_kbps = 160,
			.keyint_sec = 2,
			.keyint_max_sec = 4,
		},
};

const struct dsr_relay_limits *dsr_limits_get(void)
{
	return &limits;
}
