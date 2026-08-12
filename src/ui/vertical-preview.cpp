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

#include "vertical-preview.hpp"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include <cmath>

#include <graphics/matrix4.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>

#include "../vertical-canvas.hpp"
#include "vertical-common.hpp"

#if !defined(_WIN32) && !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif

namespace {

const float kMinBoundsPx = 40.0f;
/* HANDLE_SEL_RADIUS in OBSBasicPreview: the handles are drawn at radius 4 and
 * grabbed within 1.5 times that, so a near miss still takes. */
const double kGrabTolerancePx = 6.0;

} // namespace

VerticalPreview::VerticalPreview(VerticalCanvas *manager, QWidget *parent) : QWidget(parent), manager(manager)
{
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setMinimumHeight(280);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	/* Hover needs move events with no button held. */
	setMouseTracking(true);

	connect(manager, &VerticalCanvas::selectionChanged, this, &VerticalPreview::applySelection);
	/* Membership changes can remove the selected item; re-resolving by id
	 * drops the stale reference rather than outlining a ghost. */
	connect(manager, &VerticalCanvas::changed, this, [this]() {
		applySelection(this->manager->selectedItemId());
		/* The hovered item may have just been removed. */
		setHoveredItem(nullptr);
	});
}

VerticalPreview::~VerticalPreview()
{
	if (display) {
		obs_display_remove_draw_callback(display, drawCallback, this);
		obs_display_destroy(display);
		display = nullptr;
	}

	QMutexLocker lock(&mutex);
	if (quad || overflowTexture || overflowEffect || stripedEffect) {
		obs_enter_graphics();
		if (quad)
			gs_vertexbuffer_destroy(quad);
		if (overflowTexture)
			gs_texture_destroy(overflowTexture);
		if (overflowEffect)
			gs_effect_destroy(overflowEffect);
		if (stripedEffect)
			gs_effect_destroy(stripedEffect);
		obs_leave_graphics();
		quad = nullptr;
		overflowTexture = nullptr;
		overflowEffect = nullptr;
		stripedEffect = nullptr;
	}
	if (selected) {
		obs_sceneitem_release(selected);
		selected = nullptr;
	}
	if (hovered) {
		obs_sceneitem_release(hovered);
		hovered = nullptr;
	}
	releaseSpacingLabels();
	if (canvas) {
		obs_canvas_release(canvas);
		canvas = nullptr;
	}
}

QPaintEngine *VerticalPreview::paintEngine() const
{
	/* libobs owns this surface. */
	return nullptr;
}

QSize VerticalPreview::sizeHint() const
{
	return QSize(270, 480);
}

void VerticalPreview::setCanvas(obs_canvas_t *newCanvas)
{
	QMutexLocker lock(&mutex);
	if (canvas)
		obs_canvas_release(canvas);
	canvas = newCanvas;
	/* The selected item belongs to a scene, not the canvas object, so a
	 * refreshed canvas reference leaves it alone. */
	if (!canvas && selected) {
		obs_sceneitem_release(selected);
		selected = nullptr;
	}
}

void VerticalPreview::applySelection(qint64 itemId)
{
	setSelectedItem(itemId >= 0 ? dsrFindCounterpartItem(manager, itemId) : nullptr);
}

void VerticalPreview::setSelectedItem(obs_sceneitem_t *item)
{
	QMutexLocker lock(&mutex);
	if (selected)
		obs_sceneitem_release(selected);
	selected = item;
}

void VerticalPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	ensureDisplay();
}

void VerticalPreview::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	const qreal ratio = devicePixelRatioF();
	{
		QMutexLocker lock(&mutex);
		uiScale = (float)ratio;
	}
	if (display)
		obs_display_resize(display, (uint32_t)(width() * ratio), (uint32_t)(height() * ratio));
}

void VerticalPreview::ensureDisplay()
{
	if (display)
		return;

	const qreal ratio = devicePixelRatioF();
	{
		QMutexLocker lock(&mutex);
		uiScale = (float)ratio;
	}

	struct gs_init_data init = {};
	init.cx = (uint32_t)(width() * ratio);
	init.cy = (uint32_t)(height() * ratio);
	init.format = GS_BGRA;
	init.zsformat = GS_ZS_NONE;
#if defined(_WIN32)
	init.window.hwnd = reinterpret_cast<void *>(winId());
#elif defined(__APPLE__)
	init.window.view = reinterpret_cast<id>(winId());
#else
	init.window.id = (uint32_t)winId();
	init.window.display = obs_get_nix_platform_display();
#endif

	display = obs_display_create(&init, dsrPreviewSurroundColor());
	if (display)
		obs_display_add_draw_callback(display, drawCallback, this);
}

void VerticalPreview::drawCallback(void *param, uint32_t cx, uint32_t cy)
{
	VerticalPreview *self = static_cast<VerticalPreview *>(param);
	QMutexLocker lock(&self->mutex);
	if (!self->canvas || cx == 0 || cy == 0)
		return;

	const float scale = qMin((float)cx / kPortraitWidth, (float)cy / kPortraitHeight);
	const float viewX = ((float)cx - kPortraitWidth * scale) / 2.0f;
	const float viewY = ((float)cy - kPortraitHeight * scale) / 2.0f;

	/* One projection covering the whole widget, not just the frame, with a
	 * matrix that maps canvas units into it. OBS does the same for its
	 * editing pass: a viewport clipped to the canvas would cut off exactly
	 * the overhang the hatch and the selection box exist to show. */
	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
	gs_set_viewport(0, 0, (int)cx, (int)cy);

	if (!self->quad) {
		gs_render_start(true);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(1.0f, 0.0f);
		gs_vertex2f(0.0f, 1.0f);
		gs_vertex2f(1.0f, 1.0f);
		self->quad = gs_render_save();
	}

	const bool editable = self->selected && obs_sceneitem_visible(self->selected) &&
			      !obs_sceneitem_locked(self->selected);

	gs_matrix_push();
	gs_matrix_translate3f(viewX, viewY, 0.0f);
	gs_matrix_scale3f(scale, scale, 1.0f);

	/* Hatch first, over the item's whole box. The canvas fill then paints
	 * over everything inside the frame, leaving only the overhang. */
	if (editable)
		self->drawOverflow(self->selected);

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_technique_t *solidTech = gs_effect_get_technique(solid, "Solid");
	struct vec4 fill;
	vec4_set(&fill, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(gs_effect_get_param_by_name(solid, "color"), &fill);

	gs_technique_begin(solidTech);
	gs_technique_begin_pass(solidTech, 0);
	gs_matrix_push();
	gs_matrix_scale3f((float)kPortraitWidth, (float)kPortraitHeight, 1.0f);
	gs_load_vertexbuffer(self->quad);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_matrix_pop();
	gs_technique_end_pass(solidTech);
	gs_technique_end(solidTech);

	obs_render_canvas_texture(self->canvas);

	/* Handles are sized in display pixels, so the item scale is left
	 * behind and only the device ratio carries in. */
	if (editable) {
		self->drawSelection(self->selected, true, self->uiScale);
		self->drawSpacingHelpers(self->selected, kPortraitWidth * scale, kPortraitHeight * scale,
					 self->uiScale);
	}

	/* An unselected source under the cursor gets its box only, in the
	 * hover colour, matching what OBS's preview does. */
	if (self->hovered && self->hovered != self->selected && obs_sceneitem_visible(self->hovered) &&
	    !obs_sceneitem_locked(self->hovered))
		self->drawSelection(self->hovered, false, self->uiScale);

	gs_matrix_pop();

	gs_projection_pop();
	gs_viewport_pop();
}

bool VerticalPreview::mapToCanvas(const QPointF &widgetPos, QPointF *canvasPos) const
{
	const float scale = qMin((float)width() / kPortraitWidth, (float)height() / kPortraitHeight);
	if (scale <= 0.0f)
		return false;

	const float originX = (width() - kPortraitWidth * scale) / 2.0f;
	const float originY = (height() - kPortraitHeight * scale) / 2.0f;

	canvasPos->setX((widgetPos.x() - originX) / scale);
	canvasPos->setY((widgetPos.y() - originY) / scale);
	return true;
}

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
obs_sceneitem_t *VerticalPreview::itemAt(const QPointF &canvasPos) const
{
	obs_source_t *sceneSource = manager->currentCounterpart();
	if (!sceneSource)
		return nullptr;

	QVector<obs_sceneitem_t *> items;
	obs_scene_enum_items(obs_scene_from_source(sceneSource), dsrCollectSceneItems, &items);
	obs_source_release(sceneSource);

	obs_sceneitem_t *hit = nullptr;
	for (obs_sceneitem_t *item : items) {
		if (!obs_sceneitem_visible(item) || obs_sceneitem_locked(item))
			continue;

		struct matrix4 box;
		struct matrix4 inverse;
		obs_sceneitem_get_box_transform(item, &box);
		if (!matrix4_inv(&inverse, &box))
			continue;

		struct vec3 point;
		vec3_set(&point, (float)canvasPos.x(), (float)canvasPos.y(), 0.0f);
		vec3_transform(&point, &point, &inverse);
		if (point.x >= 0.0f && point.x <= 1.0f && point.y >= 0.0f && point.y <= 1.0f)
			hit = item;
	}

	if (hit)
		obs_sceneitem_addref(hit);
	for (obs_sceneitem_t *item : items)
		obs_sceneitem_release(item);
	return hit;
}

void VerticalPreview::setHoveredItem(obs_sceneitem_t *item)
{
	QMutexLocker lock(&mutex);
	if (hovered)
		obs_sceneitem_release(hovered);
	hovered = item;
}

QPoint VerticalPreview::mapFromCanvas(const QPointF &canvasPos) const
{
	const float scale = qMin((float)width() / kPortraitWidth, (float)height() / kPortraitHeight);
	const float originX = (width() - kPortraitWidth * scale) / 2.0f;
	const float originY = (height() - kPortraitHeight * scale) / 2.0f;
	return QPoint((int)(canvasPos.x() * scale + originX), (int)(canvasPos.y() * scale + originY));
}

void VerticalPreview::contextMenuEvent(QContextMenuEvent *event)
{
	QPointF canvasPos;
	if (!mapToCanvas(event->pos(), &canvasPos))
		return;
	showContextMenu(canvasPos);
	event->accept();
}

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
