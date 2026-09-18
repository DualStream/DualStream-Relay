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

#include "vertical-layout.hpp"

#include <QtGlobal>

#include <graphics/vec2.h>
#include <obs-module.h>
#include <plugin-support.h>

#include "vertical-geometry.hpp"

namespace {

/* Private settings on each portrait scene, saved with the scene collection:
 * the canvas size its item transforms are expressed in. A scene without them
 * was laid out at the only size the first releases ever used. */
const char *kLayoutWidthKey = "dsr_layout_width";
const char *kLayoutHeightKey = "dsr_layout_height";
const long long kFirstReleaseWidth = 1080;
const long long kFirstReleaseHeight = 1920;

struct MigrationPass {
	bool canScale;
	bool deferred;
};

/* The first release seeded items with full-canvas bounds boxes. Convert them
 * to the equivalent plain transform, preserving what is on screen: a centered
 * OUTER or INNER fit inside a bounds box at some position renders the source
 * at one computable scale and offset. A source that has not reported a size
 * yet (a capture with nothing captured, a camera before its first frame) is
 * left as it is for a later pass, since the conversion cannot be undone.
 * Idempotent, since converted items have no bounds type. */
bool migrateBoundsItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	const enum obs_bounds_type type = obs_sceneitem_get_bounds_type(item);
	if (type == OBS_BOUNDS_NONE)
		return true;

	obs_source_t *source = obs_sceneitem_get_source(item);
	const float sourceWidth = (float)obs_source_get_width(source);
	const float sourceHeight = (float)obs_source_get_height(source);

	struct vec2 bounds;
	struct vec2 pos;
	obs_sceneitem_get_bounds(item, &bounds);
	obs_sceneitem_get_pos(item, &pos);

	if (sourceWidth <= 0.0f || sourceHeight <= 0.0f || bounds.x <= 0.0f || bounds.y <= 0.0f) {
		static_cast<MigrationPass *>(param)->deferred = true;
		return true;
	}

	const float fitX = bounds.x / sourceWidth;
	const float fitY = bounds.y / sourceHeight;
	const float factor = type == OBS_BOUNDS_SCALE_OUTER ? qMax(fitX, fitY) : qMin(fitX, fitY);

	struct vec2 scale;
	struct vec2 newPos;
	vec2_set(&scale, factor, factor);
	vec2_set(&newPos, pos.x + (bounds.x - sourceWidth * factor) / 2.0f,
		 pos.y + (bounds.y - sourceHeight * factor) / 2.0f);
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_pos(item, &newPos);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	return true;
}

struct ScaleFactors {
	float x;
	float y;
};

/* Move a plain transform from the canvas size it was authored at to the
 * current one. Positions follow each axis; the item scale follows the smaller
 * factor so a source keeps its own aspect when the canvas changes shape. */
bool scaleItem(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	const ScaleFactors *factors = static_cast<ScaleFactors *>(param);

	struct vec2 pos;
	struct vec2 scale;
	obs_sceneitem_get_pos(item, &pos);
	obs_sceneitem_get_scale(item, &scale);

	const float uniform = qMin(factors->x, factors->y);
	vec2_set(&pos, pos.x * factors->x, pos.y * factors->y);
	vec2_set(&scale, scale.x * uniform, scale.y * uniform);

	obs_sceneitem_set_pos(item, &pos);
	obs_sceneitem_set_scale(item, &scale);
	return true;
}

void migrateScene(obs_source_t *sceneSource, MigrationPass *pass)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return;

	obs_scene_enum_items(scene, migrateBoundsItem, pass);

	obs_data_t *priv = obs_source_get_private_settings(sceneSource);
	obs_data_set_default_int(priv, kLayoutWidthKey, kFirstReleaseWidth);
	obs_data_set_default_int(priv, kLayoutHeightKey, kFirstReleaseHeight);
	const long long authoredWidth = obs_data_get_int(priv, kLayoutWidthKey);
	const long long authoredHeight = obs_data_get_int(priv, kLayoutHeightKey);
	const long long width = dsrPortraitWidth();
	const long long height = dsrPortraitHeight();

	if (authoredWidth == width && authoredHeight == height) {
		/* Stored explicitly, so a later size change can tell what the
		 * layout was made for. */
		obs_data_set_int(priv, kLayoutWidthKey, width);
		obs_data_set_int(priv, kLayoutHeightKey, height);
		obs_data_release(priv);
		return;
	}

	if (!pass->canScale || authoredWidth <= 0 || authoredHeight <= 0) {
		pass->deferred = true;
		obs_data_release(priv);
		return;
	}

	ScaleFactors factors = {(float)width / (float)authoredWidth, (float)height / (float)authoredHeight};
	obs_scene_enum_items(scene, scaleItem, &factors);
	obs_data_set_int(priv, kLayoutWidthKey, width);
	obs_data_set_int(priv, kLayoutHeightKey, height);
	obs_data_release(priv);

	obs_log(LOG_INFO, "portrait layout '%s' scaled from %lldx%lld to %lldx%lld", obs_source_get_name(sceneSource),
		authoredWidth, authoredHeight, width, height);
}

bool migrateCanvasScene(void *param, obs_source_t *source)
{
	migrateScene(source, static_cast<MigrationPass *>(param));
	return true;
}

} // namespace

void dsrApplyFramePlacement(obs_sceneitem_t *item, obs_source_t *source, bool fill)
{
	const float sourceWidth = (float)obs_source_get_width(source);
	const float sourceHeight = (float)obs_source_get_height(source);

	/* A real scale rather than a bounds box, because the item's box IS the
	 * source here: selection, dragging and corner resizing in the preview
	 * all read the box transform, and a full-canvas bounds box would make
	 * every item hit-test and outline as the whole scene. A source with no
	 * dimensions yet keeps scale one and gets placed properly the next time
	 * an arrangement action touches it. */
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);

	if (sourceWidth <= 0.0f || sourceHeight <= 0.0f)
		return;

	const float canvasWidth = (float)dsrPortraitWidth();
	const float canvasHeight = (float)dsrPortraitHeight();
	const float fitX = canvasWidth / sourceWidth;
	const float fitY = canvasHeight / sourceHeight;
	const float factor = fill ? qMax(fitX, fitY) : qMin(fitX, fitY);

	struct vec2 scale;
	struct vec2 pos;
	vec2_set(&scale, factor, factor);
	vec2_set(&pos, (canvasWidth - sourceWidth * factor) / 2.0f, (canvasHeight - sourceHeight * factor) / 2.0f);

	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_pos(item, &pos);
}

void dsrStampLayoutSize(obs_source_t *portraitScene)
{
	obs_data_t *priv = obs_source_get_private_settings(portraitScene);
	obs_data_set_int(priv, kLayoutWidthKey, dsrPortraitWidth());
	obs_data_set_int(priv, kLayoutHeightKey, dsrPortraitHeight());
	obs_data_release(priv);
}

bool dsrMigrateCanvasLayouts(obs_canvas_t *canvas)
{
	/* Item transforms can be rewritten at any time, but a canvas whose
	 * program is being encoded must not have every item jump mid-stream;
	 * the scale pass waits until nothing is on air. */
	MigrationPass pass = {!obs_video_active(), false};
	obs_canvas_enum_scenes(canvas, migrateCanvasScene, &pass);
	return !pass.deferred;
}
