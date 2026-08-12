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

#include "vertical-dock.hpp"

#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "../vertical-canvas.hpp"
#include "dsr-ui-common.hpp"
#include "vertical-common.hpp"
#include "vertical-preview.hpp"

VerticalDock::VerticalDock(VerticalCanvas *manager, QWidget *parent) : QWidget(parent), manager(manager)
{
	QVBoxLayout *root = new QVBoxLayout(this);
	root->setContentsMargins(0, 0, 0, 0);
	root->setSpacing(0);

	stack = new QStackedWidget;
	root->addWidget(stack, 1);

	offPage = new QWidget;
	QVBoxLayout *offLayout = new QVBoxLayout(offPage);
	offLayout->setContentsMargins(12, 12, 12, 12);
	offLayout->setSpacing(12);
	QLabel *offBody = new QLabel(dsrText("Vertical.OffBody"));
	offBody->setObjectName(QStringLiteral("bodyText"));
	offBody->setWordWrap(true);
	setupButton = new QPushButton(dsrText("Vertical.SetUp"));
	setupButton->setObjectName(QStringLiteral("primaryButton"));
	setupButton->setCursor(Qt::PointingHandCursor);
	offLayout->addWidget(offBody);
	offLayout->addWidget(setupButton, 0, Qt::AlignLeft);
	offLayout->addStretch(1);
	stack->addWidget(offPage);

	/* The preview fills the dock edge to edge. */
	preview = new VerticalPreview(manager);
	stack->addWidget(preview);

	connect(setupButton, &QPushButton::clicked, this, [this]() { toggleEnabled(true); });
	connect(manager, &VerticalCanvas::changed, this, &VerticalDock::refreshAll);

	setStyleSheet(dsrVerticalStyleSheet());
	setMinimumWidth(200);
	refreshAll();
}

QSize VerticalDock::sizeHint() const
{
	return QSize(300, 640);
}

void VerticalDock::toggleEnabled(bool on)
{
	dsrSetVerticalEnabled(this, on);
	refreshAll();
}

void VerticalDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	tryAutoEnable();
}

/* On by default: showing the dock sets the canvas up once the frontend has a
 * collection loaded to put it in. The only thing that stops it is the user
 * turning it off on purpose. */
void VerticalDock::tryAutoEnable()
{
	if (!isVisible() || manager->enabled() || !manager->ready() || dsrReadFlag(kVerticalOffFlag))
		return;
	manager->setEnabled(true);
}

void VerticalDock::refreshAll()
{
	tryAutoEnable();

	const bool on = manager->enabled();
	stack->setCurrentWidget(on ? static_cast<QWidget *>(preview) : offPage);
	preview->setCanvas(on ? manager->canvasRef() : nullptr);
}
