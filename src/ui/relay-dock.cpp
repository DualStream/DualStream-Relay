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

#include "relay-dock.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

#include "../relay-output.h"
#include "../vertical-canvas.hpp"
#include "dsr-ui-common.hpp"
#include "destination-dialog.hpp"
#include "relay-dock-text.hpp"
#include "dsr-widgets.hpp"
#include "settings-dialog.hpp"
#include "vertical-dock.hpp"

void RelayDock::setBanner(const QString &text, const char *kind, const QString &actionText,
			  std::function<void()> action)
{
	if (text.isEmpty()) {
		banner->setVisible(false);
		bannerAction->setVisible(false);
		bannerActionFn = nullptr;
		return;
	}

	banner->setText(text);
	banner->setProperty("kind", QLatin1String(kind));
	dsrRepolish(banner);
	banner->setVisible(true);

	if (actionText.isEmpty()) {
		bannerAction->setVisible(false);
		bannerActionFn = nullptr;
	} else {
		bannerAction->setText(actionText);
		bannerActionFn = std::move(action);
		bannerAction->setVisible(true);
	}
}

void RelayDock::refreshUi()
{
	const State state = computeState();

	/* Keep the vertical side current on whether anything wants the
	 * portrait canvas; it gates the second output. */
	if (VerticalCanvas::instance()) {
		bool portrait = false;
		for (const DsrDestination &dest : destinations->list()) {
			if (dest.enabled &&
			    (dest.canvas == QLatin1String("portrait") || dest.canvas == QLatin1String("both"))) {
				portrait = true;
				break;
			}
		}
		VerticalCanvas::instance()->setHasPortraitDestinations(portrait);
		/* Publishing straight to an RTMP server is what an account
		 * without a subscription gets instead of the relay. */
		VerticalCanvas::instance()->setDirectAllowed(state == State::Lapsed);
	}

	if (state == State::Protected && current != State::Protected && !status->protectedSince().isValid())
		protectedLocalSince = QDateTime::currentDateTimeUtc();
	if (state != State::Protected && current == State::Protected)
		protectedLocalSince = QDateTime();

	current = state;
	setPill(state);

	const bool showTimer = state == State::Live || state == State::Protected || state == State::Ending;
	timerLabel->setVisible(showTimer);
	if (showTimer)
		timerLabel->setText(elapsedText());

	/* A setup problem that guarantees the stream will be refused outranks
	 * every other banner: the user cannot act on anything else until it is
	 * resolved. */
	const QString blocker = blockingSetupIssue();
	if (!blocker.isEmpty() && state != State::SignedOut && state != State::Pairing) {
		setBanner(blocker, "error", QString(), nullptr);
	} else
		/* banner, one at a time, highest priority first */
		switch (state) {
		case State::Protected:
			setBanner(protectedBannerText(), "protect", QString(), nullptr);
			break;
		case State::Offline: {
			const QDateTime last = status->lastSuccessAt();
			const QString when = last.isValid() ? last.toLocalTime().toString(QStringLiteral("hh:mm:ss"))
							    : dsrText("Settings.Never");
			setBanner(QString(dsrText("Offline.Banner")).arg(when), "warn", dsrText("Button.Retry"),
				  [this]() { refreshAll(); });
			break;
		}
		case State::SignedOut:
			if (dsr_route_is_relay())
				setBanner(dsrText("Warning.SignedOutRouted"), "warn", QString(), nullptr);
			else
				setBanner(QString(), "warn", QString(), nullptr);
			break;
		case State::Ready:
		case State::Live:
			if (destinations->loaded() && destinations->enabledCount() == 0) {
				setBanner(dsrText("Warning.NoDestinations"), "error", QString(), nullptr);
			} else if (state == State::Ready && keyMismatch()) {
				setBanner(dsrText("Warning.KeyWrong"), "error", dsrText("Button.Update"),
					  [this]() { routeToRelay(); });
			} else {
				/* Nothing is wrong, so the only thing left worth
				 * saying is where the video settings and the
				 * relay's own limits disagree. Silent when they
				 * do not, which is the common case. */
				setBanner(outputMismatch(), "warn", QString(), nullptr);
			}
			break;
		default:
			setBanner(QString(), "warn", QString(), nullptr);
			break;
		}

	/* content */
	const bool listState = state == State::Ready || state == State::Live || state == State::Protected ||
			       state == State::Offline;
	if (state == State::Lapsed) {
		/* Not a dead end: one destination can still be set up here. */
		stack->setCurrentWidget(directPage);
		refreshDirectPage();
	} else if (listState) {
		stack->setCurrentWidget(listPage);
		rebuildRows();
		/* Offline shows the last known rows, visibly inert. */
		listContainer->setEnabled(state != State::Offline);
	} else {
		stack->setCurrentWidget(messagePage);
		codeRow->setVisible(false);
		urlLabel->setVisible(false);
		primaryButton->setVisible(true);

		switch (state) {
		case State::Checking:
			messageLabel->setText(dsrText("State.CheckingBody"));
			primaryButton->setVisible(false);
			break;
		case State::SignedOut:
			messageLabel->setText(pairingError.isEmpty() ? dsrText("SignedOut.Body")
								     : dsrText("SignedOut.Body") +
									       QStringLiteral("\n\n") + pairingError);
			primaryButton->setText(dsrText("Action.SignIn"));
			break;
		case State::Pairing:
			messageLabel->setText(dsrText("Pairing.Body"));
			codeLabel->setText(auth->pairingCode());
			codeRow->setVisible(true);
			urlLabel->setText(auth->pairingUrl());
			urlLabel->setVisible(true);
			primaryButton->setText(dsrText("Button.Cancel"));
			break;
		case State::Unconfigured:
			messageLabel->setText(dsrText("Unconfigured.Body"));
			primaryButton->setText(dsrText("Footer.AddDestination"));
			break;
		case State::NotRouted: {
			char *server = dsr_route_current_server();
			const QString target = server ? QString::fromUtf8(server) : dsrText("Settings.NoTarget");
			bfree(server);
			messageLabel->setText(QString(dsrText("NotRouted.Body")).arg(target));
			primaryButton->setText(dsrText("Action.UseRelay"));
			break;
		}
		case State::Ending:
			messageLabel->setText(dsrText("Ending.Body"));
			primaryButton->setVisible(false);
			break;
		default:
			break;
		}
	}

	/* footer */
	const bool liveish = state == State::Live || state == State::Protected || state == State::Ending;
	const bool configState = state == State::Ready || state == State::Unconfigured || state == State::NotRouted;
	addButton->setVisible(configState && destinations->loaded());
	addButton->setEnabled(destinations->list().size() < destinations->maxDestinations());
	countLabel->setVisible(configState && destinations->loaded());
	countLabel->setText(
		QString(dsrText("Footer.Count")).arg(destinations->list().size()).arg(destinations->maxDestinations()));
	/* Only shown while protected. When the stream is healthy, OBS's own
	 * Stop Streaming is the single stop control, and a second button
	 * beside it would end the relay session while OBS kept publishing
	 * into nothing. */
	endButton->setVisible(state == State::Protected);
	endButton->setText(dsrText("Action.EndStreamNow"));
	footer->setVisible(!compact && (configState || liveish));

	/* compact mode keeps only the header and one summary line */
	stack->setVisible(!compact);
	summaryLabel->setVisible(compact);
	if (compact)
		summaryLabel->setText(summaryText(state));
}

/* The per-second tick exists for the live elapsed clock and for the settings
 * changes OBS raises no event for. Neither matters while nothing is on screen,
 * so a closed or hidden dock costs nothing. Going live is covered without it:
 * STREAMING_STARTING refreshes the token and repairs the key. */
void RelayDock::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	refreshTick();
	tick->start();
}

void RelayDock::hideEvent(QHideEvent *event)
{
	tick->stop();
	QWidget::hideEvent(event);
}

void RelayDock::refreshTick()
{
	/* OBS raises no event for stream-settings or connected-account changes,
	 * so poll the few cheap local reads that decide what the dock shows. */
	const QString signature = environmentSignature();
	if (signature != lastEnvironment) {
		lastEnvironment = signature;
		repairEmptyKey();
		refreshUi();
	}

	if (current == State::Live || current == State::Protected || current == State::Ending) {
		timerLabel->setText(elapsedText());
		if (current == State::Protected)
			banner->setText(protectedBannerText());

		auth->ensureFreshToken();
	}
}

void RelayDock::refreshAll()
{
	destOffline = false;
	destinations->refresh();
	status->pollNow();
	if (auth->signedIn() && !targetFetched)
		fetchIngestTarget(nullptr);
	refreshUi();
}

void RelayDock::openSettings()
{
	SettingsDialog dialog(auth, status, this);
	connect(&dialog, &SettingsDialog::routeRequested, this, &RelayDock::routeToRelay);
	connect(&dialog, &SettingsDialog::restoreRequested, this, &RelayDock::restoreRoute);
	dialog.exec();
	refreshUi();
}

void RelayDock::openAddDialog()
{
	DestinationDialog dialog(auth, destinations, this);
	dialog.exec();
	destinations->refresh();
}

/* End the broadcast from the dock. OBS can still believe it is connected
 * while the relay has fallen back to the standby screen, so stop the output
 * first and let the streaming-stopping handler end the session in the usual
 * order; only end directly when OBS has already stopped. */
void RelayDock::endEverything()
{
	if (obs_frontend_streaming_active()) {
		obs_frontend_streaming_stop();
		return;
	}
	status->requestEnd();
}

void RelayDock::endStreamHotkey()
{
	if (current == State::Live || current == State::Protected)
		endEverything();
}

void RelayDock::firstRunShow()
{
	if (firstRunHandled)
		return;
	firstRunHandled = true;

	if (dsrReadFlag(kFirstRunFlag))
		return;
	dsrWriteFlag(kFirstRunFlag, true);
	showDockWindow();
}

void RelayDock::showDockWindow()
{
	/* obs_frontend_add_dock_by_id wraps this widget in a QDockWidget that
	 * starts hidden. Reaching the wrapper through the parent is the only
	 * handle a plugin gets; fail quietly if the cast does not hold. */
	QDockWidget *dock = qobject_cast<QDockWidget *>(parentWidget());
	if (dock) {
		dock->setVisible(true);
		dock->raise();
	}
}

QSize RelayDock::sizeHint() const
{
	return QSize(320, 480);
}

void RelayDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	const bool nowCompact = height() > 0 && height() < 200;
	if (nowCompact != compact) {
		compact = nowCompact;
		refreshUi();
	}
}

void RelayDock::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		firstRunShow();
		refreshAll();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		auth->ensureFreshToken();
		/* Last moment the key can still be put right, and the only one
		 * that does not depend on the dock being on screen. */
		repairEmptyKey();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		localStreamStartMs = QDateTime::currentMSecsSinceEpoch();
		status->setLivePolling(true);
		status->pollNow();
		refreshUi();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		/* The single most important call in the plugin: without it the
		 * relay reads a stop as a connection drop and holds every
		 * platform on the standby slate for the grace window. */
		if (auth->signedIn())
			status->requestEnd();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		localStreamStartMs = 0;
		status->setLivePolling(false);
		/* Backstop for a missed STOPPING event, and the poll that
		 * clears the Ending state. */
		if (auth->signedIn() && status->hasSession() && !status->ending())
			status->requestEnd();
		/* requestEnd polls as soon as the end call is acknowledged, so
		 * nothing needs to wait a fixed interval for confirmation. */
		refreshUi();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		refreshUi();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		if (auth->signedIn() && obs_frontend_streaming_active() && !status->ending())
			status->requestEnd();
		if (status->ending()) {
			/* Give the end call a bounded window to reach the
			 * server before the process goes away. */
			QEventLoop loop;
			QTimer::singleShot(1500, &loop, &QEventLoop::quit);
			connect(status, &RelayStatus::endPosted, &loop, &QEventLoop::quit);
			loop.exec();
		}
		break;
	default:
		break;
	}
}

/* Cheap fingerprint of everything outside the plugin that changes what the
 * dock should render: where the stream output points, which key it carries,
 * and whether OBS has a connected account. All local reads, no network. */
void RelayDock::openEditDialog(const QString &destinationId)
{
	for (const DsrDestination &dest : destinations->list()) {
		if (dest.id != destinationId)
			continue;
		DestinationDialog dialog(auth, destinations, dest, this);
		dialog.exec();
		destinations->refresh();
		return;
	}
}

bool RelayDock::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonRelease) {
		QWidget *row = qobject_cast<QWidget *>(watched);
		if (row) {
			const QVariant id = row->property("destId");
			QMouseEvent *me = static_cast<QMouseEvent *>(event);
			if (id.isValid() && me->button() == Qt::LeftButton &&
			    row->rect().contains(me->position().toPoint())) {
				openEditDialog(id.toString());
				return true;
			}
		}
	}
	return QWidget::eventFilter(watched, event);
}
