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

#include <obs.h>

/* Item placement on the portrait canvas, and the migrations that keep saved
 * layouts valid: bounds boxes from the first release become plain transforms,
 * and layouts authored at another canvas size are scaled to the current one. */

/* Center an item in the 9:16 frame as a plain transform: fill covers with
 * edge overflow, fit letterboxes. Sized by what the item shows after crop. */
void dsrApplyFramePlacement(obs_sceneitem_t *item, obs_source_t *source, bool fill);

/* Scale an item to cover the frame exactly on both axes, as a plain
 * transform, ignoring its aspect. */
void dsrApplyFrameStretch(obs_sceneitem_t *item);

/* Place items that were added before their source had a size, the first
 * time the size is known. */
void dsrWatchPendingPlacement(obs_scene_t *portrait);

/* Place any item of a portrait scene still waiting for its source size whose
 * size is now known. A scene that is not being rendered never raises the
 * signal the watch relies on, so this runs before one goes on air. */
void dsrPlacePendingItems(obs_source_t *portraitScene);

/* Record the canvas size a freshly seeded portrait scene was laid out at. */
void dsrStampLayoutSize(obs_source_t *portraitScene);

/* Bring every scene of the canvas up to the current layout form. Scaling a
 * layout to a new canvas size needs the mixes idle; returns false when that
 * part had to be left for a later pass. */
bool dsrMigrateCanvasLayouts(obs_canvas_t *canvas);
