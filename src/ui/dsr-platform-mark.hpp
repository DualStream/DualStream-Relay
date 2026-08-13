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

#include <QPixmap>
#include <QString>

/* Platform artwork and naming, shared by the destination rows in the dock and
 * the connected-account rows in the add dialog, so a platform looks and reads
 * the same wherever it appears. */

/* Artwork key for a platform string. Empty when there is no logo for it,
 * which is the custom RTMP case. */
QString dsrPlatformKey(const QString &platform);

/* Human name for a platform string. Empty when it is not one we know. */
QString dsrPlatformName(const QString &platform);

/* Rounded brand square with the platform's logo centered on it, rendered at
 * the display's pixel ratio. An empty key gets a neutral broadcast mark. */
QPixmap dsrPlatformMark(const QString &key, int side, qreal ratio);

/* An account label with the platform prefix the server puts on it removed.
 * Beside the platform's own logo, "twitch - gooneysaint" says twitch twice.
 * Returns an empty string when nothing is left. */
QString dsrStripPlatformPrefix(const QString &label, const QString &platform);
