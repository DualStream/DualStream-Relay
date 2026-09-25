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
#include <QEvent>
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
		refreshStudioPreview();
	});
	refreshStudioPreview();
}

VerticalPreview::~VerticalPreview()
{
	destroyDisplay();

	QMutexLocker lock(&mutex);
	if (studioScene) {
		obs_source_dec_showing(studioScene);
		obs_source_release(studioScene);
		studioScene = nullptr;
	}
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

/* In studio mode the docks edit the preview scene, so the preview draws that
 * scene itself rather than the mobile program, and holds it showing the way
 * OBS holds its own studio preview. */
void VerticalPreview::refreshStudioPreview()
{
	obs_source_t *next = manager->studioPreviewCounterpart();
	obs_source_t *previous = nullptr;
	{
		QMutexLocker lock(&mutex);
		if (next == studioScene) {
			obs_source_release(next);
			return;
		}
		previous = studioScene;
		studioScene = next;
	}
	if (next)
		obs_source_inc_showing(next);
	if (previous) {
		obs_source_dec_showing(previous);
		obs_source_release(previous);
	}
}

void VerticalPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	ensureDisplay();
}

/* Qt gives a native widget a new window when its dock is floated, docked or
 * otherwise reparented, and takes the old one away. A display is bound to
 * the window it was made for, so it goes with that window; the first paint
 * of the new one makes a display for it. A move to a screen at another
 * scale rebuilds the display the same way, since its swap chain is sized in
 * that screen's pixels. */
bool VerticalPreview::event(QEvent *event)
{
	switch (event->type()) {
	case QEvent::WinIdChange:
	case QEvent::ScreenChangeInternal:
		destroyDisplay();
		break;
	default:
		break;
	}
	return QWidget::event(event);
}

/* libobs paints this surface, so there is nothing to draw here; a paint
 * request is the one sure sign the native window is up and exposed, which
 * is when a display can be made for it. */
void VerticalPreview::paintEvent(QPaintEvent *event)
{
	ensureDisplay();
	QWidget::paintEvent(event);
}

void VerticalPreview::destroyDisplay()
{
	if (!display)
		return;
	obs_display_remove_draw_callback(display, drawCallback, this);
	obs_display_destroy(display);
	display = nullptr;
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

	const float scale = qMin((float)cx / dsrPortraitWidth(), (float)cy / dsrPortraitHeight());
	const float viewX = ((float)cx - dsrPortraitWidth() * scale) / 2.0f;
	const float viewY = ((float)cy - dsrPortraitHeight() * scale) / 2.0f;

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
	gs_matrix_scale3f((float)dsrPortraitWidth(), (float)dsrPortraitHeight(), 1.0f);
	gs_load_vertexbuffer(self->quad);
	gs_draw(GS_TRISTRIP, 0, 0);
	gs_matrix_pop();
	gs_technique_end_pass(solidTech);
	gs_technique_end(solidTech);

	if (self->studioScene) {
		/* Clipped to the frame the way OBS draws its own preview, so
		 * overhang reads as overhang rather than as broadcast. */
		gs_viewport_push();
		gs_projection_push();
		gs_matrix_push();
		gs_matrix_identity();
		gs_ortho(0.0f, (float)dsrPortraitWidth(), 0.0f, (float)dsrPortraitHeight(), -100.0f, 100.0f);
		gs_set_viewport((int)viewX, (int)viewY, (int)(dsrPortraitWidth() * scale),
				(int)(dsrPortraitHeight() * scale));
		obs_source_video_render(self->studioScene);
		gs_matrix_pop();
		gs_projection_pop();
		gs_viewport_pop();
	} else {
		obs_render_canvas_texture(self->canvas);
	}

	/* Handles are sized in display pixels, so the item scale is left
	 * behind and only the device ratio carries in. */
	if (editable) {
		self->drawSelection(self->selected, true, self->uiScale);
		self->drawSpacingHelpers(self->selected, dsrPortraitWidth() * scale, dsrPortraitHeight() * scale,
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
	const float scale = qMin((float)width() / dsrPortraitWidth(), (float)height() / dsrPortraitHeight());
	if (scale <= 0.0f)
		return false;

	const float originX = (width() - dsrPortraitWidth() * scale) / 2.0f;
	const float originY = (height() - dsrPortraitHeight() * scale) / 2.0f;

	canvasPos->setX((widgetPos.x() - originX) / scale);
	canvasPos->setY((widgetPos.y() - originY) / scale);
	return true;
}

obs_sceneitem_t *VerticalPreview::itemAt(const QPointF &canvasPos) const
{
	obs_source_t *sceneSource = manager->editingCounterpart();
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
	const float scale = qMin((float)width() / dsrPortraitWidth(), (float)height() / dsrPortraitHeight());
	const float originX = (width() - dsrPortraitWidth() * scale) / 2.0f;
	const float originY = (height() - dsrPortraitHeight() * scale) / 2.0f;
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
