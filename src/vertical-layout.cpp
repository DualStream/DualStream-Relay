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

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>
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
/* Set once a scene's first-release bounds boxes have been converted. Later
 * bounds are the user's own, set in OBS's Edit Transform dialog, and stay. */
const char *kBoundsMigratedKey = "dsr_bounds_migrated";
/* Private settings on an item: its bounds conversion is waiting for the
 * source to report a size. */
const char *kBoundsPendingKey = "dsr_bounds_pending";
/* Private settings on an item placed before its source had a size: placed
 * again, fill or fit, the first time the size is known. */
const char *kPlacePendingKey = "dsr_place_pending";
const char *kPlaceFillKey = "dsr_place_fill";
const long long kFirstReleaseWidth = 1080;
const long long kFirstReleaseHeight = 1920;

/* Item private settings are only read and written on the UI thread: obs_data
 * is not safe against a writer on another thread. */
void setItemFlag(obs_sceneitem_t *item, const char *key, bool on)
{
	obs_data_t *priv = obs_sceneitem_get_private_settings(item);
	if (on)
		obs_data_set_bool(priv, key, true);
	else
		obs_data_erase(priv, key);
	obs_data_release(priv);
}

bool itemFlag(obs_sceneitem_t *item, const char *key)
{
	obs_data_t *priv = obs_sceneitem_get_private_settings(item);
	const bool on = obs_data_get_bool(priv, key);
	obs_data_release(priv);
	return on;
}

/* The part of the source the item shows: its size less the item's crop. */
bool croppedSize(obs_sceneitem_t *item, obs_source_t *source, float *width, float *height)
{
	struct obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	*width = (float)obs_source_get_width(source) - (float)(crop.left + crop.right);
	*height = (float)obs_source_get_height(source) - (float)(crop.top + crop.bottom);
	return *width > 0.0f && *height > 0.0f;
}

struct MigrationPass {
	bool canScale;
	bool deferred;
	/* Set once the scene's first pass is done: only items left waiting
	 * for a source size are still converted. */
	bool pendingOnly;
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
	MigrationPass *pass = static_cast<MigrationPass *>(param);
	if (pass->pendingOnly && !itemFlag(item, kBoundsPendingKey))
		return true;

	const enum obs_bounds_type type = obs_sceneitem_get_bounds_type(item);
	if (type == OBS_BOUNDS_NONE) {
		setItemFlag(item, kBoundsPendingKey, false);
		return true;
	}

	float sourceWidth = 0.0f;
	float sourceHeight = 0.0f;
	croppedSize(item, obs_sceneitem_get_source(item), &sourceWidth, &sourceHeight);

	struct vec2 bounds;
	struct vec2 pos;
	obs_sceneitem_get_bounds(item, &bounds);
	obs_sceneitem_get_pos(item, &pos);

	if (sourceWidth <= 0.0f || sourceHeight <= 0.0f || bounds.x <= 0.0f || bounds.y <= 0.0f) {
		setItemFlag(item, kBoundsPendingKey, true);
		pass->deferred = true;
		return true;
	}

	/* A stretch box keeps both axes of the box; a fit or cover keeps the
	 * source aspect, centered in the box. */
	const float fitX = bounds.x / sourceWidth;
	const float fitY = bounds.y / sourceHeight;
	const bool stretch = type == OBS_BOUNDS_STRETCH;
	const float factor = type == OBS_BOUNDS_SCALE_OUTER ? qMax(fitX, fitY) : qMin(fitX, fitY);

	struct vec2 scale;
	struct vec2 newPos;
	if (stretch) {
		vec2_set(&scale, fitX, fitY);
		newPos = pos;
	} else {
		vec2_set(&scale, factor, factor);
		vec2_set(&newPos, pos.x + (bounds.x - sourceWidth * factor) / 2.0f,
			 pos.y + (bounds.y - sourceHeight * factor) / 2.0f);
	}
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_pos(item, &newPos);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	setItemFlag(item, kBoundsPendingKey, false);
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

	/* The first pass converts every bounds item and marks the scene; an
	 * item whose source had no size yet stays marked itself, and later
	 * passes convert only those. */
	obs_data_t *priv = obs_source_get_private_settings(sceneSource);
	MigrationPass scenePass = {pass->canScale, false, obs_data_get_bool(priv, kBoundsMigratedKey)};
	obs_scene_enum_items(scene, migrateBoundsItem, &scenePass);
	obs_data_set_bool(priv, kBoundsMigratedKey, true);
	if (scenePass.deferred)
		pass->deferred = true;

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

bool placeIfPending(obs_scene_t *, obs_sceneitem_t *item, void *)
{
	if (itemFlag(item, kPlacePendingKey))
		dsrApplyFramePlacement(item, obs_sceneitem_get_source(item), itemFlag(item, kPlaceFillKey));
	return true;
}

} // namespace

void dsrApplyFramePlacement(obs_sceneitem_t *item, obs_source_t *source, bool fill)
{
	/* A real scale rather than a bounds box, because the item's box IS the
	 * source here: selection, dragging and corner resizing in the preview
	 * all read the box transform, and a full-canvas bounds box would make
	 * every item hit-test and outline as the whole scene. A source with no
	 * dimensions yet is placed the first time it reports a size. */
	float sourceWidth = 0.0f;
	float sourceHeight = 0.0f;
	if (!croppedSize(item, source, &sourceWidth, &sourceHeight)) {
		const bool video = (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0;
		setItemFlag(item, kPlacePendingKey, video);
		setItemFlag(item, kPlaceFillKey, video && fill);
		return;
	}
	setItemFlag(item, kPlacePendingKey, false);
	setItemFlag(item, kPlaceFillKey, false);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_rot(item, 0.0f);

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

void dsrApplyFrameStretch(obs_sceneitem_t *item)
{
	float sourceWidth = 0.0f;
	float sourceHeight = 0.0f;
	if (!croppedSize(item, obs_sceneitem_get_source(item), &sourceWidth, &sourceHeight))
		return;

	struct vec2 scale;
	struct vec2 pos;
	vec2_set(&scale, (float)dsrPortraitWidth() / sourceWidth, (float)dsrPortraitHeight() / sourceHeight);
	vec2_zero(&pos);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_LEFT | OBS_ALIGN_TOP);
	obs_sceneitem_set_rot(item, 0.0f);
	obs_sceneitem_set_scale(item, &scale);
	obs_sceneitem_set_pos(item, &pos);
}

/* Raised when an item's transform is rebuilt: on the UI thread for an edit,
 * on the video thread when a rendered scene sees a source change size, which
 * includes the frame it first reports one. The pending flag lives in private
 * settings, so off the UI thread only the size is checked and the queued call
 * reads the flag; on the UI thread the flag is read first so a drag queues
 * nothing. The placement itself always runs queued, outside the setter that
 * raised the signal. */
static void onItemTransform(void *, calldata_t *cd)
{
	obs_sceneitem_t *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
	QCoreApplication *app = QCoreApplication::instance();
	if (!item || !app)
		return;
	const bool uiThread = QThread::currentThread() == app->thread();
	if (uiThread && !itemFlag(item, kPlacePendingKey))
		return;
	float width = 0.0f;
	float height = 0.0f;
	if (!croppedSize(item, obs_sceneitem_get_source(item), &width, &height))
		return;

	obs_sceneitem_addref(item);
	QMetaObject::invokeMethod(
		app,
		[item]() {
			if (itemFlag(item, kPlacePendingKey))
				dsrApplyFramePlacement(item, obs_sceneitem_get_source(item),
						       itemFlag(item, kPlaceFillKey));
			obs_sceneitem_release(item);
		},
		Qt::QueuedConnection);
}

void dsrPlacePendingItems(obs_source_t *portraitScene)
{
	obs_scene_t *scene = obs_scene_from_source(portraitScene);
	if (scene)
		obs_scene_enum_items(scene, placeIfPending, nullptr);
}

void dsrWatchPendingPlacement(obs_scene_t *portrait)
{
	signal_handler_t *handler = obs_source_get_signal_handler(obs_scene_get_source(portrait));
	signal_handler_disconnect(handler, "item_transform", onItemTransform, nullptr);
	signal_handler_connect(handler, "item_transform", onItemTransform, nullptr);
}

void dsrStampLayoutSize(obs_source_t *portraitScene)
{
	obs_data_t *priv = obs_source_get_private_settings(portraitScene);
	obs_data_set_int(priv, kLayoutWidthKey, dsrPortraitWidth());
	obs_data_set_int(priv, kLayoutHeightKey, dsrPortraitHeight());
	obs_data_set_bool(priv, kBoundsMigratedKey, true);
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
