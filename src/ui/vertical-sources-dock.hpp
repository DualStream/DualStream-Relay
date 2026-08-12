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

#include <QWidget>

#include <obs.h>

class QScrollArea;
class QVBoxLayout;
class VerticalCanvas;

/* One row per source of the current scene's portrait counterpart: visibility
 * switch, arrangement actions, z-order. Docks beside OBS's own Sources panel
 * and shares its selection with the preview through the canvas manager. */
class VerticalSourcesDock : public QWidget {
	Q_OBJECT

public:
	explicit VerticalSourcesDock(VerticalCanvas *manager, QWidget *parent = nullptr);

	QSize sizeHint() const override;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dragLeaveEvent(QDragLeaveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	void rebuildRows();
	void updateHighlights(qint64 selectedId);
	void updateVisibility(qint64 itemId, bool visible);
	void updateLock(qint64 itemId, bool locked);
	QWidget *makeRow(obs_sceneitem_t *item, int64_t selectedId);
	obs_sceneitem_t *resolveItem(int64_t itemId) const;

	void startDrag(QWidget *row);
	int dropRowAt(const QPoint &dockPos) const;
	void showDropIndicator(int row);
	void applyDrop(qint64 itemId, int targetRow);

	VerticalCanvas *manager;

	QScrollArea *scroll;
	QWidget *listContainer;
	QVBoxLayout *listLayout;
	QWidget *dropLine;

	QPoint pressOrigin;
	qint64 pressedItemId = -1;
};
