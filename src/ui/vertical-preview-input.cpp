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

/* Mouse and wheel handling for the vertical preview: what a press selects,
 * what a drag moves or resizes, and what the cursor says will happen. The
 * geometry behind a drag lives in vertical-preview-edit.cpp. */

#include "vertical-preview.hpp"

#include <QMouseEvent>
#include <QWheelEvent>

#include "../vertical-canvas.hpp"
#include "vertical-common.hpp"

namespace {

/* Floor for a wheel-driven scale, so an item cannot be shrunk to a size with
 * no handles left to grab. */
const float kMinBoundsPx = 40.0f;

} // namespace

void VerticalPreview::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	QPointF canvasPos;
	if (!mapToCanvas(event->position(), &canvasPos))
		return;

	/* A press on a handle of the already selected item stretches it, or
	 * crops it when alt is held, before any reselect can happen. */
	{
		QMutexLocker lock(&mutex);
		if (selected && obs_sceneitem_visible(selected) && !obs_sceneitem_locked(selected)) {
			const uint32_t handle = handleAt(selected, canvasPos);
			if (handle) {
				cropping = event->modifiers() & Qt::AltModifier;
				beginStretch(selected, handle);
				return;
			}
		}
	}

	obs_sceneitem_t *hit = itemAt(canvasPos);

	/* applySelection takes its own reference, so this one is released
	 * after the position is read from it. */
	manager->setSelectedItemId(hit ? obs_sceneitem_get_id(hit) : -1);

	if (hit) {
		dragging = true;
		dragStartCanvas = canvasPos;
		obs_sceneitem_get_pos(hit, &dragStartPos);
		obs_sceneitem_release(hit);
	}
}

/* Topmost visible unlocked item under a canvas point, referenced; the caller
 * releases it. Enumeration runs bottom to top, so the last hit wins. The box
 * transform maps the unit square onto the item, so a point is inside when its
 * inverse lands in [0, 1]. */
void VerticalPreview::leaveEvent(QEvent *event)
{
	QWidget::leaveEvent(event);
	setHoveredItem(nullptr);
	unsetCursor();
}

void VerticalPreview::mouseMoveEvent(QMouseEvent *event)
{
	QPointF canvasPos;
	if (!mapToCanvas(event->position(), &canvasPos))
		return;

	if (!dragging && !stretchHandle) {
		setHoveredItem(itemAt(canvasPos));
		updateCursor(canvasPos);
		return;
	}

	QMutexLocker lock(&mutex);
	if (!selected || obs_sceneitem_locked(selected))
		return;

	if (stretchHandle) {
		if (cropping)
			cropItem(selected, canvasPos);
		else
			stretchItem(selected, canvasPos);
		return;
	}

	struct vec2 pos;
	vec2_set(&pos, dragStartPos.x + (float)(canvasPos.x() - dragStartCanvas.x()),
		 dragStartPos.y + (float)(canvasPos.y() - dragStartCanvas.y()));
	obs_sceneitem_set_pos(selected, &pos);
}

/* Resize cursor over a handle, using the same flag-to-shape mapping OBS uses.
 * The rotation and negative-scale branches it carries are left out: this dock
 * cannot rotate or flip an item. */
void VerticalPreview::updateCursor(const QPointF &canvasPos)
{
	uint32_t flags = 0;
	{
		QMutexLocker lock(&mutex);
		if (selected && obs_sceneitem_visible(selected) && !obs_sceneitem_locked(selected))
			flags = handleAt(selected, canvasPos);
	}

	if (!flags) {
		unsetCursor();
		return;
	}

	if ((flags & DSR_ITEM_LEFT && flags & DSR_ITEM_TOP) || (flags & DSR_ITEM_RIGHT && flags & DSR_ITEM_BOTTOM))
		setCursor(Qt::SizeFDiagCursor);
	else if ((flags & DSR_ITEM_LEFT && flags & DSR_ITEM_BOTTOM) || (flags & DSR_ITEM_RIGHT && flags & DSR_ITEM_TOP))
		setCursor(Qt::SizeBDiagCursor);
	else if (flags & DSR_ITEM_LEFT || flags & DSR_ITEM_RIGHT)
		setCursor(Qt::SizeHorCursor);
	else
		setCursor(Qt::SizeVerCursor);
}

void VerticalPreview::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		dragging = false;
		stretchHandle = 0;
		cropping = false;
	}
}

void VerticalPreview::wheelEvent(QWheelEvent *event)
{
	QMutexLocker lock(&mutex);
	if (!selected || obs_sceneitem_locked(selected))
		return;

	const float factor = event->angleDelta().y() > 0 ? 1.05f : 1.0f / 1.05f;

	if (obs_sceneitem_get_bounds_type(selected) != OBS_BOUNDS_NONE) {
		/* Grow the bounds box around its center so the source stays put
		 * rather than walking toward a corner. */
		struct vec2 bounds;
		struct vec2 pos;
		obs_sceneitem_get_bounds(selected, &bounds);
		obs_sceneitem_get_pos(selected, &pos);

		const float centerX = pos.x + bounds.x / 2.0f;
		const float centerY = pos.y + bounds.y / 2.0f;
		bounds.x = qMax(bounds.x * factor, kMinBoundsPx);
		bounds.y = qMax(bounds.y * factor, kMinBoundsPx);
		vec2_set(&pos, centerX - bounds.x / 2.0f, centerY - bounds.y / 2.0f);

		obs_sceneitem_set_bounds(selected, &bounds);
		obs_sceneitem_set_pos(selected, &pos);
	} else {
		struct vec2 scale;
		obs_sceneitem_get_scale(selected, &scale);
		scale.x *= factor;
		scale.y *= factor;
		obs_sceneitem_set_scale(selected, &scale);
	}

	event->accept();
}
