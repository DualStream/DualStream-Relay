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

#include <QIcon>

#include <obs.h>

class QCheckBox;

/* OBS's own source icon for a source type, not a copy of it. The active theme
 * assigns each one to a Qt property on the main window (qproperty-imageIcon
 * and friends), which is readable from here, so these follow the theme
 * exactly and change with it. Null when the property is missing. */
QIcon dsrSourceIcon(enum obs_icon_type type);

/* Visibility and lock check boxes carrying the same class property OBS puts
 * on its own, so the active theme draws them with its own artwork. */
QCheckBox *dsrMakeVisibilityCheck();
QCheckBox *dsrMakeLockCheck();
