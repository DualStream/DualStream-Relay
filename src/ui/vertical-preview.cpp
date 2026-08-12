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
const float kHandlePx = 14.0f;
const double kGrabTolerancePx = 12.0;

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

	connect(manager, &VerticalCanvas::selectionChanged, this, &VerticalPreview::applySelection);
	/* Membership changes can remove the selected item; re-resolving by id
	 * drops the stale reference rather than outlining a ghost. */
	connect(manager, &VerticalCanvas::changed, this, [this]() { applySelection(this->manager->selectedItemId()); });
}

VerticalPreview::~VerticalPreview()
{
	if (display) {
		obs_display_remove_draw_callback(display, drawCallback, this);
		obs_display_destroy(display);
		display = nullptr;
	}

	QMutexLocker lock(&mutex);
	if (outline || quad) {
		obs_enter_graphics();
		if (outline)
			gs_vertexbuffer_destroy(outline);
		if (quad)
			gs_vertexbuffer_destroy(quad);
		obs_leave_graphics();
		outline = nullptr;
		quad = nullptr;
	}
	if (selected) {
		obs_sceneitem_release(selected);
		selected = nullptr;
	}
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
	if (display) {
		const qreal ratio = devicePixelRatioF();
		obs_display_resize(display, (uint32_t)(width() * ratio), (uint32_t)(height() * ratio));
	}
}

void VerticalPreview::ensureDisplay()
{
	if (display)
		return;

	const qreal ratio = devicePixelRatioF();
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
	const int viewWidth = (int)(kPortraitWidth * scale);
	const int viewHeight = (int)(kPortraitHeight * scale);

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, (float)kPortraitWidth, 0.0f, (float)kPortraitHeight, -100.0f, 100.0f);
	gs_set_viewport(((int)cx - viewWidth) / 2, ((int)cy - viewHeight) / 2, viewWidth, viewHeight);

	/* The canvas itself is black, distinct from the dock behind it, so the
	 * 9:16 frame reads as the edge of the picture. Sources composite over
	 * this rather than over the surround. */
	if (!self->quad) {
		gs_render_start(true);
		gs_vertex2f(0.0f, 0.0f);
		gs_vertex2f(1.0f, 0.0f);
		gs_vertex2f(0.0f, 1.0f);
		gs_vertex2f(1.0f, 1.0f);
		self->quad = gs_render_save();
	}

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *colorParam = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *solidTech = gs_effect_get_technique(solid, "Solid");
	struct vec4 fill;

	vec4_set(&fill, 0.0f, 0.0f, 0.0f, 1.0f);
	gs_effect_set_vec4(colorParam, &fill);
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

	if (self->selected && obs_sceneitem_visible(self->selected)) {
		/* Built once, in the graphics context the callback already
		 * holds, so the callback never allocates after that. */
		if (!self->outline) {
			gs_render_start(true);
			gs_vertex2f(0.0f, 0.0f);
			gs_vertex2f(1.0f, 0.0f);
			gs_vertex2f(1.0f, 1.0f);
			gs_vertex2f(0.0f, 1.0f);
			gs_vertex2f(0.0f, 0.0f);
			self->outline = gs_render_save();
		}
		struct matrix4 box;
		obs_sceneitem_get_box_transform(self->selected, &box);

		struct vec4 accent;
		vec4_set(&accent, 0.953f, 0.286f, 0.055f, 1.0f);
		gs_effect_set_vec4(colorParam, &accent);

		gs_technique_begin(solidTech);
		gs_technique_begin_pass(solidTech, 0);

		gs_matrix_push();
		gs_matrix_mul(&box);
		gs_load_vertexbuffer(self->outline);
		gs_draw(GS_LINESTRIP, 0, 0);
		gs_matrix_pop();

		/* Handles are sized in display pixels so they stay grabbable at
		 * any zoom. */
		const float handleSize = kHandlePx * ((float)kPortraitWidth / (float)viewWidth);
		const float unit[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
		for (const float *corner : unit) {
			struct vec3 point;
			vec3_set(&point, corner[0], corner[1], 0.0f);
			vec3_transform(&point, &point, &box);

			gs_matrix_push();
			gs_matrix_translate3f(point.x - handleSize / 2.0f, point.y - handleSize / 2.0f, 0.0f);
			gs_matrix_scale3f(handleSize, handleSize, 1.0f);
			gs_load_vertexbuffer(self->quad);
			gs_draw(GS_TRISTRIP, 0, 0);
			gs_matrix_pop();
		}

		gs_technique_end_pass(solidTech);
		gs_technique_end(solidTech);
	}

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

/* A press near a corner of the selected item starts a resize instead of a
 * reselect, anchored on the opposite corner. */
bool VerticalPreview::beginCornerResize(const QPointF &canvasPos)
{
	QMutexLocker lock(&mutex);
	if (!selected || !obs_sceneitem_visible(selected) || obs_sceneitem_locked(selected))
		return false;

	const float widgetScale = qMin((float)width() / kPortraitWidth, (float)height() / kPortraitHeight);
	if (widgetScale <= 0.0f)
		return false;
	const double tolerance = kGrabTolerancePx / widgetScale;

	struct matrix4 box;
	obs_sceneitem_get_box_transform(selected, &box);

	const float unit[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
	for (int i = 0; i < 4; i++) {
		struct vec3 corner;
		vec3_set(&corner, unit[i][0], unit[i][1], 0.0f);
		vec3_transform(&corner, &corner, &box);
		if (std::hypot(canvasPos.x() - corner.x, canvasPos.y() - corner.y) > tolerance)
			continue;

		struct vec3 anchor;
		vec3_set(&anchor, unit[3 - i][0], unit[3 - i][1], 0.0f);
		vec3_transform(&anchor, &anchor, &box);

		resizeAnchor = QPointF(anchor.x, anchor.y);
		resizeStartDist = std::hypot(canvasPos.x() - resizeAnchor.x(), canvasPos.y() - resizeAnchor.y());
		if (resizeStartDist < 1.0)
			return false;
		obs_sceneitem_get_pos(selected, &resizeStartPos);
		obs_sceneitem_get_scale(selected, &resizeStartScale);
		resizing = true;
		return true;
	}
	return false;
}

void VerticalPreview::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return;

	QPointF canvasPos;
	if (!mapToCanvas(event->position(), &canvasPos))
		return;

	if (beginCornerResize(canvasPos))
		return;

	obs_source_t *sceneSource = manager->currentCounterpart();
	if (!sceneSource)
		return;

	QVector<obs_sceneitem_t *> items;
	obs_scene_enum_items(obs_scene_from_source(sceneSource), dsrCollectSceneItems, &items);
	obs_source_release(sceneSource);

	/* Topmost hit wins: enumeration runs bottom to top, so walk backwards.
	 * The transform maps the unit square onto the item's box, so a point is
	 * inside when its inverse lands in [0, 1]. */
	obs_sceneitem_t *hit = nullptr;
	for (int i = items.size() - 1; i >= 0; i--) {
		obs_sceneitem_t *item = items[i];
		if (hit || !obs_sceneitem_visible(item) || obs_sceneitem_locked(item))
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

	/* applySelection takes its own reference, so every enumerated
	 * reference is released below. */
	manager->setSelectedItemId(hit ? obs_sceneitem_get_id(hit) : -1);

	if (hit) {
		dragging = true;
		dragStartCanvas = canvasPos;
		obs_sceneitem_get_pos(hit, &dragStartPos);
	}

	for (obs_sceneitem_t *item : items)
		obs_sceneitem_release(item);
}

void VerticalPreview::mouseMoveEvent(QMouseEvent *event)
{
	if (!dragging && !resizing)
		return;

	QPointF canvasPos;
	if (!mapToCanvas(event->position(), &canvasPos))
		return;

	QMutexLocker lock(&mutex);
	if (!selected || obs_sceneitem_locked(selected))
		return;

	if (resizing) {
		const double dist = std::hypot(canvasPos.x() - resizeAnchor.x(), canvasPos.y() - resizeAnchor.y());
		const float factor = (float)qBound(0.05, dist / resizeStartDist, 50.0);

		struct vec2 scale;
		struct vec2 pos;
		vec2_set(&scale, resizeStartScale.x * factor, resizeStartScale.y * factor);
		vec2_set(&pos, (float)resizeAnchor.x() + (resizeStartPos.x - (float)resizeAnchor.x()) * factor,
			 (float)resizeAnchor.y() + (resizeStartPos.y - (float)resizeAnchor.y()) * factor);
		obs_sceneitem_set_scale(selected, &scale);
		obs_sceneitem_set_pos(selected, &pos);
		return;
	}

	struct vec2 pos;
	vec2_set(&pos, dragStartPos.x + (float)(canvasPos.x() - dragStartCanvas.x()),
		 dragStartPos.y + (float)(canvasPos.y() - dragStartCanvas.y()));
	obs_sceneitem_set_pos(selected, &pos);
}

void VerticalPreview::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton) {
		dragging = false;
		resizing = false;
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
