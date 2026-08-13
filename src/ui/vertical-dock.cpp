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

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "../relay-direct.hpp"
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

	QWidget *livePage = new QWidget;
	QVBoxLayout *liveLayout = new QVBoxLayout(livePage);
	liveLayout->setContentsMargins(0, 0, 0, 0);
	liveLayout->setSpacing(0);

	/* The preview fills the dock edge to edge. */
	preview = new VerticalPreview(manager);
	liveLayout->addWidget(preview, 1);

	/* Only shown when a destination takes the mobile program and the relay
	 * is not carrying it. OBS's Start Streaming always sends the desktop
	 * canvas, so without this the mobile program has no way on air. */
	goLiveBar = new QWidget;
	QHBoxLayout *goLiveLayout = new QHBoxLayout(goLiveBar);
	goLiveLayout->setContentsMargins(8, 6, 8, 6);
	goLiveLayout->setSpacing(8);
	goLiveButton = new QPushButton;
	goLiveButton->setObjectName(QStringLiteral("primaryButton"));
	goLiveButton->setCursor(Qt::PointingHandCursor);
	goLiveLayout->addWidget(goLiveButton);
	goLiveLayout->addStretch();

	/* Being on air with nothing on screen saying so is how somebody streams
	 * for an hour without meaning to. */
	livePill = new QLabel(dsrText("Vertical.LiveNow"));
	livePill->setObjectName(QStringLiteral("statusPill"));
	livePill->setProperty("state", QStringLiteral("onair"));
	livePill->setVisible(false);
	goLiveLayout->addWidget(livePill);
	goLiveBar->setVisible(false);
	liveLayout->addWidget(goLiveBar);
	stack->addWidget(livePage);

	connect(goLiveButton, &QPushButton::clicked, this, &VerticalDock::toggleDirect);
	connect(setupButton, &QPushButton::clicked, this, [this]() { toggleEnabled(true); });
	connect(manager, &VerticalCanvas::changed, this, &VerticalDock::refreshAll);
	connect(manager, &VerticalCanvas::publishingChanged, this, [this](bool) { refreshAll(); });
	connect(manager, &VerticalCanvas::directPhaseChanged, this, &VerticalDock::refreshAll);

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
	stack->setCurrentIndex(on ? 1 : 0);
	preview->setCanvas(on ? manager->canvasRef() : nullptr);

	/* The bar belongs to the locally held destination only. When the relay
	 * carries the mobile program it starts and stops with OBS and nothing
	 * here should offer a second way to do it. A publish already running
	 * keeps its control whatever the account now says, so subscribing
	 * mid-stream can never strand it with no way to stop. */
	const DsrDirectDestination direct = dsrDirectLoad();
	const VerticalCanvas::DirectPhase phase = manager->directPhase();
	const bool mine = (on && manager->directAllowed() && !direct.isEmpty() && direct.isPortrait()) ||
			  phase != VerticalCanvas::DirectPhase::Idle;

	goLiveBar->setVisible(mine);
	if (mine)
		applyDirectPhase(phase);
}

/* Connecting and disconnecting both take a moment, and both say so. A control
 * that looks identical while it works is one the user presses again. */
void VerticalDock::applyDirectPhase(VerticalCanvas::DirectPhase phase)
{
	const bool busy = phase == VerticalCanvas::DirectPhase::Starting ||
			  phase == VerticalCanvas::DirectPhase::Stopping;
	const bool onAir = phase == VerticalCanvas::DirectPhase::Live || phase == VerticalCanvas::DirectPhase::Stopping;

	const char *label = "Vertical.StartMobile";
	if (phase == VerticalCanvas::DirectPhase::Starting)
		label = "Vertical.StartingMobile";
	else if (phase == VerticalCanvas::DirectPhase::Live)
		label = "Vertical.StopMobile";
	else if (phase == VerticalCanvas::DirectPhase::Stopping)
		label = "Vertical.StoppingMobile";

	goLiveButton->setText(dsrText(label));
	goLiveButton->setEnabled(!busy);
	goLiveButton->setObjectName(onAir ? QStringLiteral("endButton") : QStringLiteral("primaryButton"));
	dsrRepolish(goLiveButton);

	livePill->setVisible(onAir);
}

void VerticalDock::toggleDirect()
{
	if (manager->publishing()) {
		manager->stopDirect();
		return;
	}

	const DsrDirectDestination direct = dsrDirectLoad();
	if (direct.isEmpty() || !direct.isPortrait())
		return;

	if (!manager->startDirect(direct.url, direct.key))
		QMessageBox::warning(this, dsrText("Vertical.DockTitle"), dsrText("Vertical.StartMobileFailed"));
}
