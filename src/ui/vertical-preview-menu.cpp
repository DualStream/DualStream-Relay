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

/* Right-click menu for the vertical preview, carrying the entries OBS puts on
 * its own: add a source, transform, order, and the property and filter
 * dialogs, which the frontend API exposes so they are OBS's real ones rather
 * than copies. */

#include "vertical-preview.hpp"

#include <functional>

#include <QAction>
#include <QInputDialog>
#include <QMenu>

#include <obs-frontend-api.h>
#include <obs-module.h>

#include "../vertical-canvas.hpp"
#include "dsr-source-icon.hpp"
#include "dsr-ui-common.hpp"
#include "vertical-common.hpp"

namespace {

/* Apply one of OBS's screen-fitting transforms, matching
 * CenterAlignSelectedItems. */
void fitToCanvas(obs_sceneitem_t *item, enum obs_bounds_type boundsType)
{
	struct obs_transform_info info;
	vec2_set(&info.pos, 0.0f, 0.0f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	info.rot = 0.0f;
	vec2_set(&info.bounds, (float)kPortraitWidth, (float)kPortraitHeight);
	info.bounds_type = boundsType;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.crop_to_bounds = obs_sceneitem_get_bounds_crop(item);
	obs_sceneitem_set_info2(item, &info);
}

void resetTransform(obs_sceneitem_t *item)
{
	struct obs_transform_info info;
	vec2_set(&info.pos, 0.0f, 0.0f);
	vec2_set(&info.scale, 1.0f, 1.0f);
	info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
	info.rot = 0.0f;
	vec2_set(&info.bounds, 0.0f, 0.0f);
	info.bounds_type = OBS_BOUNDS_NONE;
	info.bounds_alignment = OBS_ALIGN_CENTER;
	info.crop_to_bounds = false;
	obs_sceneitem_set_info2(item, &info);

	struct obs_sceneitem_crop crop = {0, 0, 0, 0};
	obs_sceneitem_set_crop(item, &crop);
}

void centerOnCanvas(obs_sceneitem_t *item)
{
	struct matrix4 box;
	obs_sceneitem_get_box_transform(item, &box);

	struct vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	pos.x += ((float)kPortraitWidth - box.x.x) / 2.0f - box.t.x;
	pos.y += ((float)kPortraitHeight - box.y.y) / 2.0f - box.t.y;
	obs_sceneitem_set_pos(item, &pos);
}

void flipItem(obs_sceneitem_t *item, bool horizontal)
{
	struct obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);
	if (horizontal)
		info.scale.x = -info.scale.x;
	else
		info.scale.y = -info.scale.y;
	obs_sceneitem_set_info2(item, &info);
}

/* A name no existing source is using, the way OBS disambiguates. */
QString uniqueSourceName(const QString &base)
{
	QString name = base;
	for (int suffix = 2; suffix < 1000; suffix++) {
		obs_source_t *existing = obs_get_source_by_name(name.toUtf8().constData());
		if (!existing)
			return name;
		obs_source_release(existing);
		name = QStringLiteral("%1 %2").arg(base).arg(suffix);
	}
	return name;
}

} // namespace

/* Create a source and put it on both scenes. Membership is kept matched
 * between a landscape scene and its portrait counterpart, so adding to the
 * vertical side alone would see it removed again on the next reconcile. */
void VerticalPreview::addSource(const char *id)
{
	obs_source_t *sceneSource = manager->currentCounterpart();
	if (!sceneSource)
		return;

	obs_source_t *landscapeScene = obs_frontend_get_current_scene();
	if (!landscapeScene) {
		obs_source_release(sceneSource);
		return;
	}

	const char *displayName = obs_source_get_display_name(id);
	const QString name = uniqueSourceName(QString::fromUtf8(displayName ? displayName : id));
	obs_source_t *source = obs_source_create(id, name.toUtf8().constData(), nullptr, nullptr);
	if (source) {
		obs_scene_add(obs_scene_from_source(landscapeScene), source);
		obs_sceneitem_t *item = obs_scene_add(obs_scene_from_source(sceneSource), source);
		if (item) {
			VerticalCanvas::placeItem(item, false);
			manager->setSelectedItemId(obs_sceneitem_get_id(item));
		}
		/* The properties dialog is where a new source is actually
		 * configured, so open it the way OBS does. */
		obs_frontend_open_source_properties(source);
		obs_source_release(source);
	}

	obs_source_release(landscapeScene);
	obs_source_release(sceneSource);
}

void VerticalPreview::buildAddSourceMenu(QMenu *menu)
{
	const char *id = nullptr;
	const char *unversioned = nullptr;
	for (size_t i = 0; obs_enum_input_types2(i, &id, &unversioned); i++) {
		const uint32_t caps = obs_get_source_output_flags(id);
		if (caps & OBS_SOURCE_CAP_DISABLED)
			continue;
		/* Video only: an audio-only source has nothing to place on a
		 * canvas, and OBS keeps those out of its preview menu too. */
		if (!(caps & OBS_SOURCE_VIDEO))
			continue;

		const char *label = obs_source_get_display_name(id);
		if (!label)
			continue;

		const QString sourceId = QString::fromUtf8(id);
		QAction *action = menu->addAction(QString::fromUtf8(label));
		action->setIcon(dsrSourceIcon(obs_source_get_icon_type(id)));
		connect(action, &QAction::triggered, this,
			[this, sourceId]() { addSource(sourceId.toUtf8().constData()); });
	}
}

void VerticalPreview::showContextMenu(const QPointF &canvasPos)
{
	obs_sceneitem_t *item = itemAt(canvasPos);
	if (item)
		manager->setSelectedItemId(obs_sceneitem_get_id(item));

	QMenu menu(this);

	QMenu *add = menu.addMenu(dsrText("Vertical.AddSource"));
	buildAddSourceMenu(add);

	if (item) {
		const qint64 itemId = obs_sceneitem_get_id(item);
		auto onItem = [this, itemId](std::function<void(obs_sceneitem_t *)> action) {
			return [this, itemId, action]() {
				obs_sceneitem_t *target = dsrFindCounterpartItem(manager, itemId);
				if (target) {
					action(target);
					obs_sceneitem_release(target);
				}
			};
		};

		menu.addSeparator();

		QMenu *transform = menu.addMenu(dsrText("Vertical.Transform"));
		transform->addAction(dsrText("Vertical.EditTransform"), this, onItem([](obs_sceneitem_t *target) {
					     obs_frontend_open_sceneitem_edit_transform(target);
				     }));
		transform->addSeparator();
		transform->addAction(dsrText("Vertical.ResetTransform"), this,
				     onItem([](obs_sceneitem_t *target) { resetTransform(target); }));
		transform->addAction(dsrText("Vertical.FitToScreen"), this, onItem([](obs_sceneitem_t *target) {
					     fitToCanvas(target, OBS_BOUNDS_SCALE_INNER);
				     }));
		transform->addAction(dsrText("Vertical.StretchToScreen"), this,
				     onItem([](obs_sceneitem_t *target) { fitToCanvas(target, OBS_BOUNDS_STRETCH); }));
		transform->addAction(dsrText("Vertical.CenterToScreen"), this,
				     onItem([](obs_sceneitem_t *target) { centerOnCanvas(target); }));
		transform->addSeparator();
		transform->addAction(dsrText("Vertical.FlipHorizontal"), this,
				     onItem([](obs_sceneitem_t *target) { flipItem(target, true); }));
		transform->addAction(dsrText("Vertical.FlipVertical"), this,
				     onItem([](obs_sceneitem_t *target) { flipItem(target, false); }));

		QMenu *order = menu.addMenu(dsrText("Vertical.Order"));
		order->addAction(dsrText("Vertical.MoveToTop"), this, onItem([](obs_sceneitem_t *target) {
					 obs_sceneitem_set_order(target, OBS_ORDER_MOVE_TOP);
				 }));
		order->addAction(dsrText("Vertical.MoveUp"), this, onItem([](obs_sceneitem_t *target) {
					 obs_sceneitem_set_order(target, OBS_ORDER_MOVE_UP);
				 }));
		order->addAction(dsrText("Vertical.MoveDown"), this, onItem([](obs_sceneitem_t *target) {
					 obs_sceneitem_set_order(target, OBS_ORDER_MOVE_DOWN);
				 }));
		order->addAction(dsrText("Vertical.MoveToBottom"), this, onItem([](obs_sceneitem_t *target) {
					 obs_sceneitem_set_order(target, OBS_ORDER_MOVE_BOTTOM);
				 }));

		menu.addSeparator();
		menu.addAction(dsrText("Vertical.Filters"), this, onItem([](obs_sceneitem_t *target) {
				       obs_frontend_open_source_filters(obs_sceneitem_get_source(target));
			       }));
		menu.addAction(dsrText("Vertical.Properties"), this, onItem([](obs_sceneitem_t *target) {
				       obs_frontend_open_source_properties(obs_sceneitem_get_source(target));
			       }));

		obs_sceneitem_release(item);
	}

	menu.exec(mapToGlobal(mapFromCanvas(canvasPos)));
}
