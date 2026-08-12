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

#include <initializer_list>

#include <QJsonObject>
#include <QString>

/* Shared between the auth transport and the pairing flow. Not part of the
 * class's public surface, so it stays out of relay-auth.hpp. */

const int kDefaultPollIntervalMs = 5000;
const qint64 kDefaultPairingWindowMs = 15 * 60 * 1000;

/* The pairing and token endpoints have carried more than one spelling of the
 * same field across revisions, so both readers take a list and use the first
 * key that is present and usable. */
QString dsrPickString(const QJsonObject &obj, std::initializer_list<const char *> keys);
qint64 dsrPickInt(const QJsonObject &obj, std::initializer_list<const char *> keys);
