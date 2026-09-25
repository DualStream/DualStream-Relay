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

/* Portrait scene management: seeding a counterpart for each landscape scene,
 * keeping membership in step, and the signals that report changes back. */

#include "vertical-canvas.hpp"

#include <QMetaObject>
#include <QSet>
#include <QVector>

#include <graphics/vec2.h>
#include <obs-module.h>
#include <plugin-support.h>

#include "vertical-layout.hpp"

namespace {

QString sourceUuid(obs_source_t *source)
{
	return QString::fromUtf8(obs_source_get_uuid(source));
}

/* Release the source ref carried by a scene from
 * obs_canvas_get_scene_by_name. */
void releaseScene(obs_scene_t *scene)
{
	if (scene)
		obs_source_release(obs_scene_get_source(scene));
}

bool collectCanvasSceneNames(void *param, obs_source_t *source)
{
	QVector<QString> *names = static_cast<QVector<QString> *>(param);
	if (obs_scene_from_source(source))
		names->append(QString::fromUtf8(obs_source_get_name(source)));
	return true;
}

struct SeedEntry {
	obs_source_t *source;
	bool visible;
	double area;
};

bool hasVideo(obs_source_t *source)
{
	return source && (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) != 0;
}

bool collectVideoPresence(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	if (!hasVideo(obs_sceneitem_get_source(item)))
		return true;
	*static_cast<bool *>(param) = true;
	return false;
}

/* Footprint in the landscape frame decides which source is the main
 * content. Bounds are authoritative when set; otherwise the raw size times
 * the item scale. */
SeedEntry seedEntryFor(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	double area = 0;
	if (obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE) {
		struct vec2 bounds;
		obs_sceneitem_get_bounds(item, &bounds);
		area = (double)bounds.x * (double)bounds.y;
	} else {
		struct vec2 scale;
		obs_sceneitem_get_scale(item, &scale);
		area = (double)obs_source_get_width(source) * scale.x * (double)obs_source_get_height(source) * scale.y;
	}
	return {source, obs_sceneitem_visible(item), qAbs(area)};
}

bool collectSeedEntries(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	QVector<SeedEntry> *entries = static_cast<QVector<SeedEntry> *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (!source)
		return true;

	/* One portrait item per source, whatever the landscape scene does.
	 * The first occurrence keeps its z-position. */
	for (const SeedEntry &entry : *entries) {
		if (entry.source == source)
			return true;
	}

	entries->append(seedEntryFor(item));
	return true;
}

/* The largest visible source, shown filling the frame when a layout is
 * first made; NULL when none has a size yet. */
const SeedEntry *pickHero(const QVector<SeedEntry> &entries)
{
	const SeedEntry *hero = nullptr;
	for (const SeedEntry &entry : entries) {
		if (entry.visible && entry.area > 0 && (!hero || entry.area > hero->area))
			hero = &entry;
	}
	return hero;
}

struct MembershipEntry {
	obs_sceneitem_t *item;
	QString uuid;
};

bool collectMembership(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	QVector<MembershipEntry> *entries = static_cast<QVector<MembershipEntry> *>(param);
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (source)
		entries->append({item, sourceUuid(source)});
	return true;
}

} // namespace

void VerticalCanvas::placeItem(obs_sceneitem_t *item, bool fill)
{
	dsrApplyFramePlacement(item, obs_sceneitem_get_source(item), fill);
}

void VerticalCanvas::reconcileScenes()
{
	if (!canvas)
		return;

	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);

	QSet<QString> names;
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *scene = scenes.sources.array[i];
		names.insert(QString::fromUtf8(obs_source_get_name(scene)));
		seedCounterpart(scene);
		connectSceneSignals(scene);
	}
	obs_frontend_source_list_free(&scenes);

	/* Bring saved layouts up to date before anything reads item boxes; a
	 * no-op on scenes already in the current form. Scaling to a changed
	 * canvas size waits for the mixes to be idle, and converting a first
	 * release layout waits for its source to report a size; either is
	 * said once rather than at every pass until then. */
	const bool wasDeferred = layoutsDeferred;
	layoutsDeferred = !dsrMigrateCanvasLayouts(canvas);
	if (layoutsDeferred && !wasDeferred)
		obs_log(LOG_INFO, "some portrait layouts keep their previous form until they can be migrated");

	QVector<QString> canvasScenes;
	obs_canvas_enum_scenes(canvas, collectCanvasSceneNames, &canvasScenes);
	for (const QString &name : canvasScenes) {
		if (names.contains(name))
			continue;
		obs_scene_t *orphan = obs_canvas_get_scene_by_name(canvas, name.toUtf8().constData());
		if (orphan) {
			obs_source_t *source = obs_scene_get_source(orphan);
			obs_canvas_scene_remove(orphan);
			obs_source_release(source);
		}
	}

	/* Drop connection entries whose scene died; the connections themselves
	 * went down with the scene's signal handler. */
	for (auto it = connectedScenes.begin(); it != connectedScenes.end();) {
		obs_source_t *alive = obs_weak_source_get_source(it.value());
		if (alive) {
			obs_source_release(alive);
			++it;
		} else {
			obs_weak_source_release(it.value());
			it = connectedScenes.erase(it);
		}
	}
}

void VerticalCanvas::seedCounterpart(obs_source_t *landscapeScene)
{
	obs_scene_t *existing = obs_canvas_get_scene_by_name(canvas, obs_source_get_name(landscapeScene));
	if (existing) {
		connectCounterpartSignals(existing);
		releaseScene(existing);
		return;
	}

	obs_scene_t *landscape = obs_scene_from_source(landscapeScene);
	if (!landscape)
		return;

	obs_scene_t *portrait = obs_canvas_scene_create(canvas, obs_source_get_name(landscapeScene));
	if (!portrait) {
		obs_log(LOG_WARNING, "portrait scene for '%s' could not be created",
			obs_source_get_name(landscapeScene));
		return;
	}
	connectCounterpartSignals(portrait);
	dsrStampLayoutSize(obs_scene_get_source(portrait));

	QVector<SeedEntry> entries;
	obs_scene_enum_items(landscape, collectSeedEntries, &entries);
	const SeedEntry *hero = pickHero(entries);

	for (const SeedEntry &entry : entries) {
		obs_sceneitem_t *item = obs_scene_add(portrait, entry.source);
		if (!item)
			continue;
		const bool isHero = hero == &entry;
		dsrApplyFramePlacement(item, entry.source, isHero);
		obs_sceneitem_set_visible(item, isHero);
	}

	obs_log(LOG_INFO, "portrait scene seeded for '%s' (%d sources)", obs_source_get_name(landscapeScene),
		(int)entries.size());
}

void VerticalCanvas::connectSceneSignals(obs_source_t *landscapeScene)
{
	const QString uuid = sourceUuid(landscapeScene);
	if (connectedScenes.contains(uuid))
		return;

	signal_handler_t *handler = obs_source_get_signal_handler(landscapeScene);
	signal_handler_connect(handler, "item_add", onItemsChanged, this);
	signal_handler_connect(handler, "item_remove", onItemsChanged, this);
	signal_handler_connect(handler, "rename", onSceneRenamed, this);

	connectedScenes.insert(uuid, obs_source_get_weak_source(landscapeScene));
}

void VerticalCanvas::disconnectAllSceneSignals()
{
	for (auto it = connectedScenes.begin(); it != connectedScenes.end(); ++it) {
		obs_source_t *scene = obs_weak_source_get_source(it.value());
		if (scene) {
			signal_handler_t *handler = obs_source_get_signal_handler(scene);
			signal_handler_disconnect(handler, "item_add", onItemsChanged, this);
			signal_handler_disconnect(handler, "item_remove", onItemsChanged, this);
			signal_handler_disconnect(handler, "rename", onSceneRenamed, this);
			obs_source_release(scene);
		}
		obs_weak_source_release(it.value());
	}
	connectedScenes.clear();
}

/* Membership sync, run queued on the UI thread. The portrait scene carries the
 * set of distinct sources its landscape scene has: additions arrive hidden so
 * nothing reaches the vertical stream unreviewed, and items whose source left
 * the landscape scene go away. Layout of surviving items is never touched.
 * A portrait scene with no items has no layout to protect, as when it was
 * seeded from a new, empty scene: its first additions are laid out the way a
 * seed is, so the mobile frame shows what the desktop one does. */
void VerticalCanvas::syncMembership(const QString &sceneUuid)
{
	if (!canvas)
		return;

	obs_source_t *landscapeSource = obs_get_source_by_uuid(sceneUuid.toUtf8().constData());
	if (!landscapeSource)
		return;

	obs_scene_t *landscape = obs_scene_from_source(landscapeSource);
	obs_scene_t *portrait = obs_canvas_get_scene_by_name(canvas, obs_source_get_name(landscapeSource));
	if (!landscape || !portrait) {
		releaseScene(portrait);
		obs_source_release(landscapeSource);
		return;
	}

	QVector<MembershipEntry> landscapeItems;
	QVector<MembershipEntry> portraitItems;
	obs_scene_enum_items(landscape, collectMembership, &landscapeItems);
	obs_scene_enum_items(portrait, collectMembership, &portraitItems);

	QSet<QString> landscapeSet;
	for (const MembershipEntry &entry : landscapeItems)
		landscapeSet.insert(entry.uuid);

	QSet<QString> portraitSet;
	for (const MembershipEntry &entry : portraitItems) {
		if (!landscapeSet.contains(entry.uuid))
			obs_sceneitem_remove(entry.item);
		else
			portraitSet.insert(entry.uuid);
	}

	/* Audio sources draw nothing, so a scene holding only those still has
	 * no layout to protect. */
	bool hasLayout = false;
	obs_scene_enum_items(portrait, collectVideoPresence, &hasLayout);
	const bool firstLayout = !hasLayout;
	QVector<SeedEntry> additions;
	for (const MembershipEntry &entry : landscapeItems) {
		if (portraitSet.contains(entry.uuid))
			continue;
		portraitSet.insert(entry.uuid);
		additions.append(seedEntryFor(entry.item));
	}

	/* A source that has not reported a size yet cannot be the fill hero; the
	 * first video source the desktop shows stands in for it, filling the
	 * frame once its size is known. */
	const SeedEntry *hero = firstLayout ? pickHero(additions) : nullptr;
	const SeedEntry *shown = hero;
	for (int i = 0; firstLayout && !shown && i < additions.size(); i++) {
		if (additions[i].visible && hasVideo(additions[i].source))
			shown = &additions[i];
	}

	for (const SeedEntry &addition : additions) {
		obs_sceneitem_t *item = obs_scene_add(portrait, addition.source);
		if (item) {
			dsrApplyFramePlacement(item, addition.source, &addition == shown);
			obs_sceneitem_set_visible(item, &addition == shown);
		}
	}

	releaseScene(portrait);
	obs_source_release(landscapeSource);
	emit changed();
}

void VerticalCanvas::renameCounterpart(const QString &prevName, const QString &newName)
{
	if (!canvas)
		return;

	obs_scene_t *portrait = obs_canvas_get_scene_by_name(canvas, prevName.toUtf8().constData());
	if (!portrait)
		return;

	obs_source_set_name(obs_scene_get_source(portrait), newName.toUtf8().constData());
	releaseScene(portrait);
	emit changed();
}

void VerticalCanvas::onItemsChanged(void *data, calldata_t *cd)
{
	VerticalCanvas *self = static_cast<VerticalCanvas *>(data);
	obs_scene_t *scene = static_cast<obs_scene_t *>(calldata_ptr(cd, "scene"));
	if (!scene)
		return;

	/* Take the uuid synchronously, while the calldata pointers are valid,
	 * and do the real work on the UI thread. */
	const QString uuid = sourceUuid(obs_scene_get_source(scene));
	QMetaObject::invokeMethod(self, [self, uuid]() { self->syncMembership(uuid); }, Qt::QueuedConnection);
}

namespace {

struct SelectionPick {
	obs_source_t *editing;
	int64_t id;
};

struct SelectionMatch {
	bool here;
	int64_t id;
};

bool selectMatching(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	const SelectionMatch *match = static_cast<SelectionMatch *>(param);
	const bool want = match->here && obs_sceneitem_get_id(item) == match->id;
	if (obs_sceneitem_selected(item) != want)
		obs_sceneitem_select(item, want);
	return true;
}

bool selectInScene(void *param, obs_source_t *source)
{
	const SelectionPick *pick = static_cast<SelectionPick *>(param);
	obs_scene_t *scene = obs_scene_from_source(source);
	if (scene) {
		SelectionMatch match = {source == pick->editing, pick->id};
		obs_scene_enum_items(scene, selectMatching, &match);
	}
	return true;
}

} // namespace

/* The scene's own selection follows the dock's, in every mobile scene, so an
 * OBS Edit Transform dialog opened on a mobile item tracks the selection and a
 * scene edited before a switch is not left holding one. */
void VerticalCanvas::mirrorSelection(int64_t id)
{
	if (!canvas)
		return;
	SelectionPick pick = {editingCounterpart(), id};
	obs_canvas_enum_scenes(canvas, selectInScene, &pick);
	obs_source_release(pick.editing);
}

/* Watch a portrait scene for the two changes the docks render, an item's
 * visibility and z-order, and for sources that report a size after being
 * added. Connecting is idempotent because libobs ignores a duplicate callback
 * and data pair. */
void VerticalCanvas::connectCounterpartSignals(obs_scene_t *portrait)
{
	signal_handler_t *handler = obs_source_get_signal_handler(obs_scene_get_source(portrait));
	signal_handler_disconnect(handler, "item_visible", onItemVisible, this);
	signal_handler_disconnect(handler, "item_locked", onItemLocked, this);
	signal_handler_disconnect(handler, "reorder", onSceneReordered, this);
	signal_handler_connect(handler, "item_visible", onItemVisible, this);
	signal_handler_connect(handler, "item_locked", onItemLocked, this);
	signal_handler_connect(handler, "reorder", onSceneReordered, this);
	dsrWatchPendingPlacement(portrait);
}

void VerticalCanvas::onItemVisible(void *data, calldata_t *cd)
{
	VerticalCanvas *self = static_cast<VerticalCanvas *>(data);
	obs_sceneitem_t *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
	if (!item)
		return;

	const qint64 id = obs_sceneitem_get_id(item);
	const bool visible = calldata_bool(cd, "visible");
	QMetaObject::invokeMethod(
		self, [self, id, visible]() { emit self->itemVisibilityChanged(id, visible); }, Qt::QueuedConnection);
}

void VerticalCanvas::onItemLocked(void *data, calldata_t *cd)
{
	VerticalCanvas *self = static_cast<VerticalCanvas *>(data);
	obs_sceneitem_t *item = static_cast<obs_sceneitem_t *>(calldata_ptr(cd, "item"));
	if (!item)
		return;

	const qint64 id = obs_sceneitem_get_id(item);
	const bool locked = calldata_bool(cd, "locked");
	QMetaObject::invokeMethod(
		self, [self, id, locked]() { emit self->itemLockChanged(id, locked); }, Qt::QueuedConnection);
}

void VerticalCanvas::onSceneReordered(void *data, calldata_t *)
{
	VerticalCanvas *self = static_cast<VerticalCanvas *>(data);
	QMetaObject::invokeMethod(self, [self]() { emit self->changed(); }, Qt::QueuedConnection);
}

void VerticalCanvas::onSceneRenamed(void *data, calldata_t *cd)
{
	VerticalCanvas *self = static_cast<VerticalCanvas *>(data);
	const QString prev = QString::fromUtf8(calldata_string(cd, "prev_name"));
	const QString next = QString::fromUtf8(calldata_string(cd, "new_name"));
	QMetaObject::invokeMethod(
		self, [self, prev, next]() { self->renameCounterpart(prev, next); }, Qt::QueuedConnection);
}
