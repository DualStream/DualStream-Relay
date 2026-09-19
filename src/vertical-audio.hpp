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

#include <functional>

#include <obs.h>

/* Audio for the vertical stream.
 *
 * OBS routes audio per source, not per canvas: a source feeds whichever of
 * the six audio tracks it is a member of, and every output encodes one
 * track. The vertical stream therefore gets a track of its own, the highest
 * one no OBS output uses, and a source is heard on it when it is a member
 * of that track. Everything is a member by default, so the vertical stream
 * carries the same audio as the main one until a source is switched off. */

struct DsrVerticalAudioTrack {
	/* Zero-based mixer index the vertical output encodes. */
	int mixer;
	/* False when every track is spoken for by an OBS output, in which
	 * case the mixer above is the stream's own and per-source control is
	 * not available. */
	bool dedicated;
};

/* Read from the current profile's output settings. */
DsrVerticalAudioTrack dsrVerticalAudioTrack();

bool dsrSourceHasAudio(obs_source_t *source);
bool dsrVerticalAudioOn(obs_source_t *source, int mixer);
void dsrSetVerticalAudioOn(obs_source_t *source, int mixer, bool on);

/* The global audio devices from Settings, Audio (Desktop Audio, Mic/Aux),
 * which are not scene items but are usually the first thing left out of a
 * phone feed. Each source is referenced only for the duration of the call. */
void dsrEnumAudioDevices(const std::function<void(obs_source_t *)> &visit);
