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

#include <QColor>
#include <QString>
#include <QVector>

#include <obs.h>

#include "../vertical-geometry.hpp"

class VerticalCanvas;

/* Shared by the preview and both vertical docks. */

/* Which side of an item a drag is acting on. Same bit layout OBS uses for its
 * own handles, and the same values as OBS_ALIGN_*, which is what lets the crop
 * code compare a handle against an item's alignment directly. */
#define DSR_ITEM_LEFT (1 << 0)
#define DSR_ITEM_RIGHT (1 << 1)
#define DSR_ITEM_TOP (1 << 2)
#define DSR_ITEM_BOTTOM (1 << 3)

/* Settings key recording that the user turned the canvas off on purpose. */
extern const char *kVerticalOffFlag;

/* Colour of the dock behind the 9:16 frame, for obs_display_create. The frame
 * itself is filled black by the preview so the canvas edges read clearly
 * against it. */
uint32_t dsrPreviewSurroundColor();

/* The colour OBS paints behind its own preview. The overflow hatch is a 13
 * percent translucent wash, so its appearance is entirely decided by what sits
 * under it; reproducing OBS's look means reproducing that backdrop. Themes
 * assign it to a Qt property on OBSQTDisplay, a class a plugin cannot
 * subclass, so it is read off the live main-window preview and falls back to
 * the frontend's own default. */
QColor dsrObsPreviewBackground();

/* Item of the current portrait scene by id, referenced; caller releases.
 * NULL when the scene or the item is gone. */
obs_sceneitem_t *dsrFindCounterpartItem(VerticalCanvas *manager, int64_t itemId);

/* obs_scene_enum_items callback collecting referenced items into a
 * QVector<obs_sceneitem_t *>; the caller releases each one. */
bool dsrCollectSceneItems(obs_scene_t *scene, obs_sceneitem_t *item, void *param);

QString dsrVerticalStyleSheet();
