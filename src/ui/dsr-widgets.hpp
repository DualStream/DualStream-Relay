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

#include <QAbstractButton>
#include <QColor>

class QMenu;

/* Painted controls.
 *
 * Qt's stock QCheckBox and QToolButton carry their own decorations: a check
 * box indicator, and a menu arrow drawn underneath whatever text the button
 * already has. Both fight the design language, and neither can be fully
 * removed through a style sheet on every OBS theme. These draw themselves. */

/** Track-and-knob toggle, matching the switch in the DualStream mixer. */
class DsrSwitch : public QAbstractButton {
	Q_OBJECT

public:
	explicit DsrSwitch(QWidget *parent = nullptr);

	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	bool hovered = false;
};

/** Square icon button with no text and no menu indicator. Attaching a menu
 *  pops it under the button on click rather than letting Qt draw an arrow. */
class DsrIconButton : public QAbstractButton {
	Q_OBJECT

public:
	enum class Glyph {
		Kebab,    /* three dots, the per-row overflow menu */
		Settings, /* sliders, the dock header action */
		Plus,     /* add, the destination list footer action */
		Pencil,   /* edit, on a row that is showing rather than editing */
	};

	explicit DsrIconButton(Glyph glyph, QWidget *parent = nullptr);

	void setMenu(QMenu *menu);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	Glyph glyph;
	QMenu *menu = nullptr;
	bool hovered = false;
};
