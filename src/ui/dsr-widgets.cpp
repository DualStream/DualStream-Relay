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

#include "dsr-widgets.hpp"

#include <QMenu>
#include <QPainter>
#include <QPainterPath>

namespace {

/* action.secondary from the DualStream palette, the same blue its mixer
 * toggles use. */
const QColor kSwitchOn(0x0C, 0x8C, 0xE9);

/* Neutral track and glyph colors are derived from the theme's text color so
 * the controls read correctly on light and dark OBS themes alike. */
QColor tint(const QColor &base, int alpha)
{
	QColor c = base;
	c.setAlpha(alpha);
	return c;
}

} // namespace

DsrSwitch::DsrSwitch(QWidget *parent) : QAbstractButton(parent)
{
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
}

QSize DsrSwitch::sizeHint() const
{
	return QSize(34, 20);
}

void DsrSwitch::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QColor fg = palette().color(QPalette::Text);
	const qreal h = 18.0;
	const qreal w = 32.0;
	const qreal y = (height() - h) / 2.0;
	const QRectF track(0.5, y, w, h);

	QColor trackColor = isChecked() ? kSwitchOn : tint(fg, hovered ? 60 : 42);
	if (!isEnabled())
		trackColor.setAlpha(40);
	painter.setPen(Qt::NoPen);
	painter.setBrush(trackColor);
	painter.drawRoundedRect(track, h / 2.0, h / 2.0);

	const qreal knob = h - 4.0;
	const qreal knobX = isChecked() ? track.right() - knob - 2.0 : track.left() + 2.0;
	painter.setBrush(isEnabled() ? QColor(0xFF, 0xFF, 0xFF) : QColor(0xC8, 0xC8, 0xC8));
	painter.drawEllipse(QRectF(knobX, y + 2.0, knob, knob));
}

void DsrSwitch::enterEvent(QEnterEvent *)
{
	hovered = true;
	update();
}

void DsrSwitch::leaveEvent(QEvent *)
{
	hovered = false;
	update();
}

DsrIconButton::DsrIconButton(Glyph glyph, QWidget *parent) : QAbstractButton(parent), glyph(glyph)
{
	setCursor(Qt::PointingHandCursor);
	setFocusPolicy(Qt::TabFocus);
	connect(this, &QAbstractButton::clicked, this, [this]() {
		if (menu)
			menu->popup(mapToGlobal(QPoint(width() - menu->sizeHint().width(), height() + 2)));
	});
}

void DsrIconButton::setMenu(QMenu *newMenu)
{
	menu = newMenu;
}

QSize DsrIconButton::sizeHint() const
{
	return QSize(26, 26);
}

void DsrIconButton::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);

	const QColor fg = palette().color(QPalette::Text);

	if (hovered) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(tint(fg, 20));
		painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 6, 6);
	}

	painter.setBrush(tint(fg, hovered ? 220 : 150));
	painter.setPen(Qt::NoPen);

	const QPointF center(width() / 2.0, height() / 2.0);

	if (glyph == Glyph::Kebab) {
		for (int i = -1; i <= 1; i++)
			painter.drawEllipse(QPointF(center.x(), center.y() + i * 5.0), 1.7, 1.7);
		return;
	}

	if (glyph == Glyph::Plus) {
		QPen plusPen(tint(fg, hovered ? 230 : 170));
		plusPen.setWidthF(1.8);
		plusPen.setCapStyle(Qt::RoundCap);
		painter.setPen(plusPen);
		const qreal arm = 6.0;
		painter.drawLine(QPointF(center.x() - arm, center.y()), QPointF(center.x() + arm, center.y()));
		painter.drawLine(QPointF(center.x(), center.y() - arm), QPointF(center.x(), center.y() + arm));
		return;
	}

	if (glyph == Glyph::Pencil) {
		/* Body and tip as one silhouette, which stays legible at this size
		 * where an outlined pencil turns to mush. */
		QPainterPath pencil;
		pencil.moveTo(center.x() - 5.5, center.y() + 5.5);
		pencil.lineTo(center.x() - 4.2, center.y() + 1.4);
		pencil.lineTo(center.x() + 2.4, center.y() - 5.2);
		pencil.lineTo(center.x() + 5.2, center.y() - 2.4);
		pencil.lineTo(center.x() - 1.4, center.y() + 4.2);
		pencil.closeSubpath();
		painter.setBrush(tint(fg, hovered ? 230 : 170));
		painter.drawPath(pencil);
		return;
	}

	/* Settings: two sliders, each a track with a knob at a different stop.
	 * Reads as "settings" at 26px far better than a toothed gear. */
	QPen pen(tint(fg, hovered ? 220 : 150));
	pen.setWidthF(1.6);
	pen.setCapStyle(Qt::RoundCap);
	painter.setPen(pen);

	const qreal x0 = center.x() - 6.0;
	const qreal x1 = center.x() + 6.0;
	const qreal yTop = center.y() - 3.5;
	const qreal yBottom = center.y() + 3.5;
	painter.drawLine(QPointF(x0, yTop), QPointF(x1, yTop));
	painter.drawLine(QPointF(x0, yBottom), QPointF(x1, yBottom));

	painter.setPen(Qt::NoPen);
	painter.setBrush(tint(fg, hovered ? 230 : 170));
	painter.drawEllipse(QPointF(center.x() + 2.5, yTop), 2.4, 2.4);
	painter.drawEllipse(QPointF(center.x() - 2.5, yBottom), 2.4, 2.4);
}

void DsrIconButton::enterEvent(QEnterEvent *)
{
	hovered = true;
	update();
}

void DsrIconButton::leaveEvent(QEvent *)
{
	hovered = false;
	update();
}
