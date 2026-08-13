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

#include "../vertical-canvas.hpp"

class QLabel;
class QPushButton;
class QStackedWidget;
class VerticalPreview;

/* The portrait canvas preview. Every pixel the dock can spare goes to the 9:16
 * frame, so there is no header and no hint line; turning the canvas off lives
 * in the preview's context menu.
 *
 * The one exception is the bar along the bottom, and only when a locally held
 * destination takes the mobile program. OBS's Start Streaming always carries
 * the desktop canvas, so that bar is the only control that can put this one on
 * air, and it reports where the publish has got to while it does. */
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
	void toggleDirect();
	void applyDirectPhase(VerticalCanvas::DirectPhase phase);
	void tryAutoEnable();

	VerticalCanvas *manager;

	QStackedWidget *stack;
	QWidget *offPage;
	QWidget *goLiveBar = nullptr;
	QLabel *livePill = nullptr;
	QPushButton *goLiveButton = nullptr;
	QPushButton *setupButton;
	VerticalPreview *preview;
};
