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

#include <QSet>
#include <QString>

/* Local cache of the RTMP stream keys the user typed.
 *
 * The relay is where a key actually lives, and it never hands one back, so
 * without a copy on this machine the edit dialog can only offer a blank field
 * and the user has to find the key again to change anything else about the
 * destination. This keeps one, the way the desktop app keeps its own.
 *
 * Every value is encrypted by the operating system's own facility before it
 * touches the disk, tied to the signed-in user account. There is deliberately
 * no plaintext path: on a platform with no such facility wired up yet the
 * store reports itself unavailable and stays empty, and the dialogs fall back
 * to leaving the key field blank. */

/* The pair the relay takes together. Both are kept, not just the key: the
 * relay replaces the two as a unit, so a cached key with no server beside it
 * would still make the user go and find the server again. */
struct DsrRtmpTarget {
	QString url;
	QString key;

	bool isEmpty() const { return url.isEmpty() && key.isEmpty(); }
};

/* False when this platform has no secret store wired up. Callers should treat
 * a cached target as an optional convenience and never require one. */
bool dsrSecretsAvailable();

/* Keyed by destination id. Storing an empty target forgets it. */
void dsrSecretStore(const QString &destinationId, const DsrRtmpTarget &target);
DsrRtmpTarget dsrSecretLoad(const QString &destinationId);
void dsrSecretForget(const QString &destinationId);

/* Drop everything not in `keepIds`, so a destination removed from another
 * device does not leave its key behind here. */
void dsrSecretPrune(const QSet<QString> &keepIds);

/* The same protection, for callers that keep a file of their own. Both return
 * an empty string when the platform has no store or the value cannot be
 * recovered, which the caller must treat as "no value". */
QString dsrSecretProtectText(const QString &plain);
QString dsrSecretUnprotectText(const QString &protectedText);

/* Sign-out: the account is gone, so its keys go with it. */
void dsrSecretForgetAll();
