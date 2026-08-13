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

#include <QString>

/* The one RTMP destination an account without a subscription can set up.
 *
 * It lives on this machine and nowhere else. Nothing about it is sent to
 * DualStream, and it takes one canvas at a time: the landscape program goes
 * out through OBS's own stream output, the portrait program through this
 * plugin's, and picking one replaces the other.
 *
 * Stored with the same protection as everything else this plugin keeps, so a
 * platform with no secret store wired up cannot hold one at all rather than
 * writing a stream key out in the clear. */
struct DsrDirectDestination {
	QString url;
	QString key;
	/* "landscape" or "portrait". */
	QString canvas;

	bool isEmpty() const { return url.isEmpty() || key.isEmpty(); }
	bool isPortrait() const { return canvas == QLatin1String("portrait"); }
};

/* False when this platform cannot protect a stream key, in which case nothing
 * is stored and the destination cannot be offered. */
bool dsrDirectAvailable();

DsrDirectDestination dsrDirectLoad();

/* False when the value could not be protected, which is the only way this
 * fails; the caller should say so rather than assume it saved. */
bool dsrDirectStore(const DsrDirectDestination &destination);

void dsrDirectForget();
