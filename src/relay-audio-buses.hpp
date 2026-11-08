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

#include <stdint.h>

#include <QVector>

/* The audio buses the relay contribution carries.
 *
 * OBS encodes an audio track per source membership, and over SRT it sends
 * every track its streaming output is given: Settings, Output, Streaming,
 * Audio Tracks. The relay numbers what arrives from zero in the order it
 * arrives, so the first track ticked there is bus 0, the second is bus 1,
 * and a destination is served the bus it asks for by that number. The track
 * NUMBER is not the bus: ticking tracks 1 and 3 sends buses 0 and 1.
 *
 * This is the one place that reads which tracks the stream encodes, so the
 * portrait canvas can keep off them and a destination can be offered the
 * ones it could actually receive. */

/* The tracks the relay stream encodes, one bit per track, bit 0 being
 * track 1. Never zero: OBS refuses a streaming output with no track, and an
 * unset setting is its default of track 1 alone. */
uint32_t dsrStreamTrackMask();

/* Whether this profile can send more than one track at all. Only Advanced
 * output mode ticks the streaming tracks; Simple mode sends one, so there
 * is no second bus to point a destination at until the profile is moved. */
bool dsrAudioTracksAvailable();

struct DsrAudioBus {
	/* What the destination asks for, and the PID offset on the wire. */
	int bus;
	/* The OBS track it is, counting from 1, as Settings names it. */
	int obsTrack;
};

/* Every bus the next stream carries, in bus order. One entry means every
 * destination receives the same audio, which is what OBS sends until a
 * second track is ticked. */
QVector<DsrAudioBus> dsrContributionAudioBuses();

/* The buses a destination may be pointed at: the carried ones the relay
 * lets a destination choose between. The relay's own selection stops at bus
 * 3, so a fifth or sixth OBS track travels but cannot be asked for. */
QVector<DsrAudioBus> dsrSelectableAudioBuses();
