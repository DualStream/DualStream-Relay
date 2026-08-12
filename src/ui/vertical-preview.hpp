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

private:
	void ensureDisplay();
	bool mapToCanvas(const QPointF &widgetPos, QPointF *canvasPos) const;
	void setSelectedItem(obs_sceneitem_t *item);
	void applySelection(qint64 itemId);
	bool beginCornerResize(const QPointF &canvasPos);

	static void drawCallback(void *param, uint32_t cx, uint32_t cy);

	VerticalCanvas *manager;
	obs_display_t *display = nullptr;

	/* Shared with the graphics thread; everything below is only touched
	 * while holding the mutex. */
	mutable QMutex mutex;
	obs_canvas_t *canvas = nullptr;
	obs_sceneitem_t *selected = nullptr;
	gs_vertbuffer_t *outline = nullptr;
	/* Unit quad, drawn as the black canvas fill and as the corner handles. */
	gs_vertbuffer_t *quad = nullptr;

	bool dragging = false;
	QPointF dragStartCanvas;
	struct vec2 dragStartPos;

	bool resizing = false;
	QPointF resizeAnchor;
	double resizeStartDist = 0;
	struct vec2 resizeStartPos;
	struct vec2 resizeStartScale;
};
