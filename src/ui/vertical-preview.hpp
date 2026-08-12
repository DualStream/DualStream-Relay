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

#include <QMutex>
#include <QPointF>
#include <QWidget>

#include <graphics/matrix4.h>
#include <graphics/vec4.h>
#include <graphics/vec2.h>
#include <obs.h>

class VerticalCanvas;

/* Live 9:16 view of the portrait canvas, drawn by libobs into this widget's
 * native window, and the editing surface for it. Click selects the topmost
 * source under the cursor, dragging moves it, a corner drag scales it around
 * the opposite corner, the wheel scales around the center. Selection is held
 * by the canvas manager so the sources dock stays in step. */
class VerticalPreview : public QWidget {
	Q_OBJECT

public:
	explicit VerticalPreview(VerticalCanvas *manager, QWidget *parent = nullptr);
	~VerticalPreview() override;

	/* Takes ownership of the reference; pass NULL to clear. */
	void setCanvas(obs_canvas_t *newCanvas);

	QSize sizeHint() const override;

protected:
	QPaintEngine *paintEngine() const override;
	void showEvent(QShowEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	void ensureDisplay();
	bool mapToCanvas(const QPointF &widgetPos, QPointF *canvasPos) const;
	void setSelectedItem(obs_sceneitem_t *item);
	void applySelection(qint64 itemId);
	/* Implemented in vertical-preview-edit.cpp. */
	uint32_t handleAt(obs_sceneitem_t *item, const QPointF &canvasPos) const;
	void beginStretch(obs_sceneitem_t *item, uint32_t handle);
	void stretchItem(obs_sceneitem_t *item, const QPointF &canvasPos);
	void cropItem(obs_sceneitem_t *item, const QPointF &canvasPos);
	void updateCursor(const QPointF &canvasPos);

	static void drawCallback(void *param, uint32_t cx, uint32_t cy);

	/* Implemented in vertical-preview-draw.cpp. */
	void drawOverflow(obs_sceneitem_t *item);
	void drawSelection(obs_sceneitem_t *item, bool selected, float pixelRatio);
	void drawCropSide(bool cropped, float x1, float y1, float x2, float y2, float thickness, struct vec2 boxScale,
			  const struct vec4 &colour);
	void drawSpacingHelpers(obs_sceneitem_t *item, float viewWidth, float viewHeight, float pixelRatio);
	void releaseSpacingLabels();

	/* The item under the cursor, tracked so an unselected source can be
	 * outlined on hover the way OBS's preview does. Referenced. */
	void setHoveredItem(obs_sceneitem_t *item);
	obs_sceneitem_t *itemAt(const QPointF &canvasPos) const;

	VerticalCanvas *manager;
	obs_display_t *display = nullptr;

	/* Shared with the graphics thread; everything below is only touched
	 * while holding the mutex. */
	mutable QMutex mutex;
	obs_canvas_t *canvas = nullptr;
	obs_sceneitem_t *selected = nullptr;
	obs_sceneitem_t *hovered = nullptr;
	/* Unit quad, drawn as the black canvas fill and as the resize handles. */
	gs_vertbuffer_t *quad = nullptr;
	/* OBS's own diagonal hatch, carried in this plugin's data directory. */
	gs_texture_t *overflowTexture = nullptr;
	gs_effect_t *overflowEffect = nullptr;
	gs_effect_t *stripedEffect = nullptr;
	/* Device pixel ratio, cached because the draw callback runs on the
	 * graphics thread and must not call into Qt. */
	float uiScale = 1.0f;

	/* One text source per canvas edge, created on first use. Private
	 * sources, so they never appear in the user's source list. */
	obs_source_t *spacingLabel[4] = {nullptr, nullptr, nullptr, nullptr};
	int spacingPx[4] = {-1, -1, -1, -1};

	bool dragging = false;
	QPointF dragStartCanvas;
	struct vec2 dragStartPos;

	/* Non-zero while a handle is being dragged; alt at press turns the same
	 * drag into a crop. */
	uint32_t stretchHandle = 0;
	bool cropping = false;
	struct vec2 stretchSize;
	struct obs_sceneitem_crop startCrop;
	struct matrix4 itemToScreen;
	struct matrix4 screenToItem;
};
