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

#include <QCheckBox>

#include <obs.h>

/* One line of the "Heard on the vertical stream" list: the source's name
 * with a plain check box, ticked when the source is heard there. It follows
 * the source's own track membership signal, so a change made in OBS's
 * Advanced Audio Properties moves this box too. */
class VerticalAudioToggle : public QCheckBox {
	Q_OBJECT

public:
	VerticalAudioToggle(obs_source_t *source, int mixer, QWidget *parent = nullptr);
	~VerticalAudioToggle() override;

private:
	static void onMixersChanged(void *data, calldata_t *cd);

	obs_weak_source_t *weak;
	const int mixer;
};

/* Which vertical deliveries the choices reach: the portrait stream sent to
 * destinations of its own, which they do, and Twitch dual format, which they
 * do not, since it is one broadcast with one audio. */
struct DsrVerticalDelivery {
	bool separateStream;
	bool dualFormat;
};

/* The section under the scene's rows: a heading that says what the list is,
 * then every source of the portrait scene that has audio and every global
 * audio device, each with its box. Without a track of its own for the
 * vertical stream there is nothing to choose, and the section says why
 * instead. Null when there is nothing to show. */
QWidget *dsrMakeVerticalAudioSection(obs_source_t *portraitScene, int mixer, bool dedicated,
				     DsrVerticalDelivery delivery);
