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

#include <QByteArray>
#include <QString>

/* Shared between the ladder's state and its prepare flow, both of which run
 * off the UI thread. */

/* The website and the bearer the dock last pushed. */
void dsrLadderAuth(QByteArray &apiBase, QByteArray &bearer);

/* The dock's note for this session, and whether a prepared configuration
 * is on the website waiting to be taken down once the session ends. */
void dsrLadderSetNote(const QString &note);
void dsrLadderSetPreparedPending(bool pending);
