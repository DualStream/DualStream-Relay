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

#include "vertical-audio.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>
#include <util/dstr.h>

namespace {

const uint32_t kAllTracks = (1u << MAX_AUDIO_MIXES) - 1;

/* OBS keeps its global audio devices on these output channels of the main
 * view: two desktop devices, then up to four microphones. */
const uint32_t kFirstDeviceChannel = 1;
const uint32_t kLastDeviceChannel = 6;

uint32_t trackBit(uint64_t oneBased)
{
	if (oneBased < 1 || oneBased > MAX_AUDIO_MIXES)
		return 1u;
	return 1u << (oneBased - 1);
}

/* A track mask setting; an unset one means track 1, which is what OBS
 * defaults every such setting to. */
uint32_t trackMask(config_t *config, const char *section, const char *key)
{
	const uint64_t mask = config_get_uint(config, section, key);
	return mask ? (uint32_t)(mask & kAllTracks) : 1u;
}

} // namespace

DsrVerticalAudioTrack dsrVerticalAudioTrack()
{
	uint32_t used = 1u;
	int streamMixer = 0;

	config_t *config = obs_frontend_get_profile_config();
	if (config) {
		const char *mode = config_get_string(config, "Output", "Mode");
		if (mode && astrcmpi(mode, "Advanced") == 0) {
			const uint32_t stream = trackBit(config_get_uint(config, "AdvOut", "TrackIndex"));
			used |= stream;
			for (int mixer = 0; mixer < MAX_AUDIO_MIXES; mixer++) {
				if (stream & (1u << mixer))
					streamMixer = mixer;
			}
			used |= trackMask(config, "AdvOut", "RecTracks");
			const char *recording = config_get_string(config, "AdvOut", "RecType");
			if (recording && astrcmpi(recording, "FFmpeg") == 0)
				used |= trackMask(config, "AdvOut", "FFAudioMixes");
			if (config_get_bool(config, "AdvOut", "VodTrackEnabled"))
				used |= trackBit(config_get_uint(config, "AdvOut", "VodTrackIndex"));
		} else {
			used |= trackMask(config, "SimpleOutput", "RecTracks");
		}
	}

	for (int mixer = MAX_AUDIO_MIXES - 1; mixer >= 0; mixer--) {
		if (!(used & (1u << mixer)))
			return {mixer, true};
	}
	return {streamMixer, false};
}

bool dsrSourceHasAudio(obs_source_t *source)
{
	return source && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0;
}

bool dsrVerticalAudioOn(obs_source_t *source, int mixer)
{
	return source && (obs_source_get_audio_mixers(source) & (1u << mixer)) != 0;
}

void dsrSetVerticalAudioOn(obs_source_t *source, int mixer, bool on)
{
	if (!source)
		return;
	const uint32_t mixers = obs_source_get_audio_mixers(source);
	const uint32_t wanted = on ? (mixers | (1u << mixer)) : (mixers & ~(1u << mixer));
	if (wanted != mixers)
		obs_source_set_audio_mixers(source, wanted);
}

void dsrEnumAudioDevices(const std::function<void(obs_source_t *)> &visit)
{
	for (uint32_t channel = kFirstDeviceChannel; channel <= kLastDeviceChannel; channel++) {
		obs_source_t *device = obs_get_output_source(channel);
		if (!device)
			continue;
		visit(device);
		obs_source_release(device);
	}
}
