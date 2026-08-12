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

class QPushButton;
class QStackedWidget;
class VerticalCanvas;
class VerticalPreview;

/* The portrait canvas preview and nothing else. Every pixel of the dock goes
 * to the 9:16 frame, so there is no header and no hint line; turning the
 * canvas off lives in the preview's context menu. */
class VerticalDock : public QWidget {
	Q_OBJECT

public:
	explicit VerticalDock(VerticalCanvas *manager, QWidget *parent = nullptr);

	QSize sizeHint() const override;

protected:
	void showEvent(QShowEvent *event) override;

private:
	void refreshAll();
	void toggleEnabled(bool on);
	void tryAutoEnable();

	VerticalCanvas *manager;

	QStackedWidget *stack;
	QWidget *offPage;
	QPushButton *setupButton;
	VerticalPreview *preview;
};
