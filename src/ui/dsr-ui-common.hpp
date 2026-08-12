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

class QFrame;
class QLabel;
class QWidget;

/* Helpers every dock in this plugin needs. They live here so the docks share
 * one implementation rather than each carrying its own copy. */

QString dsrText(const char *key);

/* Display name of a destination's canvas: "landscape", "portrait" or "both". */
QString dsrCanvasDisplay(const QString &canvas);

/* Horizontal rule between sections of a dialog. */
QFrame *dsrMakeSeparator();

/* Bold section heading from a locale key. */
QLabel *dsrMakeSectionHeader(const char *key);

/* Re-evaluate a widget's style after a dynamic property changed. */
void dsrRepolish(QWidget *widget);

/* Boolean flags in the module's settings.json. Read-modify-write, because
 * several docks keep their own keys in the same file. */
bool dsrReadFlag(const char *key);
void dsrWriteFlag(const char *key, bool value);

/* Style rules shared by every dock: scroll bars, muted and body text. */
QString dsrSharedStyleSheet();
