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

#include "relay-audio-buses.hpp"

#include <media-io/audio-io.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/dstr.h>

namespace {

/* The relay lets a destination name buses 0 to 3. A track beyond the fourth
 * still travels, and the relay still carries it, but nothing can be pointed
 * at it, so it is not offered. */
const int kMaxSelectableBus = 3;

} // namespace

bool dsrAudioTracksAvailable()
{
	config_t *config = obs_frontend_get_profile_config();
	if (!config)
		return false;
	const char *mode = config_get_string(config, "Output", "Mode");
	return mode && astrcmpi(mode, "Advanced") == 0;
}

uint32_t dsrStreamTrackMask()
{
	/* Simple output mode encodes one track and offers no choice of it.
	 * Advanced mode is where the tracks are ticked, and over SRT the
	 * streaming output takes the ticked set rather than the single track
	 * the other protocols use. */
	config_t *config = obs_frontend_get_profile_config();
	if (!config || !dsrAudioTracksAvailable())
		return 1u;

	const uint32_t all = (1u << MAX_AUDIO_MIXES) - 1;
	const uint32_t mask = (uint32_t)(config_get_uint(config, "AdvOut", "StreamMultiTrackAudioMixes") & all);
	return mask ? mask : 1u;
}

QVector<DsrAudioBus> dsrContributionAudioBuses()
{
	QVector<DsrAudioBus> buses;
	const uint32_t mask = dsrStreamTrackMask();
	int bus = 0;
	for (int track = 0; track < MAX_AUDIO_MIXES; track++) {
		if (!(mask & (1u << track)))
			continue;
		buses.append({bus, track + 1});
		bus++;
	}
	return buses;
}

QVector<DsrAudioBus> dsrSelectableAudioBuses()
{
	QVector<DsrAudioBus> buses = dsrContributionAudioBuses();
	while (buses.size() > kMaxSelectableBus + 1)
		buses.removeLast();
	return buses;
}
