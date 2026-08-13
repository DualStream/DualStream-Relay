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

#include <QDateTime>
#include <QHash>
#include <QStringList>
#include <QWidget>

#include <obs-frontend-api.h>

#include "../relay-auth.hpp"
#include "../relay-output.h"
#include "../relay-destinations.hpp"
#include "../relay-status.hpp"
#include "dsr-widgets.hpp"

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

/* The relay dock. One rule shapes everything in here: there is exactly one
 * Start Streaming button and it is the one OBS already has. The dock routes
 * the profile's stream output to the relay with the user's consent, reports
 * what the relay is doing, and ends the session cleanly when OBS stops. It
 * never carries video and never adds a second start control. */
class RelayDock : public QWidget {
	Q_OBJECT

public:
	explicit RelayDock(QWidget *parent = nullptr);

	void handleFrontendEvent(enum obs_frontend_event event);
	void showDockWindow();

	/* A sane opening size for the floating dock; without this it opens
	 * small enough that compact mode hides everything but the pill. */
	QSize sizeHint() const override;

public slots:
	void endStreamHotkey();

protected:
	void resizeEvent(QResizeEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	enum class State {
		Checking,
		SignedOut,
		Pairing,
		Lapsed,
		Unconfigured,
		NotRouted,
		Ready,
		Live,
		Protected,
		Ending,
		Offline,
	};

	State computeState() const;
	void refreshUi();
	void rebuildRows();
	void refreshTick();
	void refreshAll();
	void fetchIngestTarget(std::function<void(bool ok)> done);
	void routeToRelay();
	void offerEncoderTune();
	QStringList encoderTuneChanges(const dsr_encoder_settings &current) const;
	void restoreRoute();
	void openSettings();
	void openAddDialog();
	void firstRunShow();
	bool keyMismatch() const;
	void repairEmptyKey();
	void endEverything();
	QString environmentSignature() const;
	QString blockingSetupIssue() const;
	void openEditDialog(const QString &destinationId);
	QWidget *makeRow(const DsrDestination &dest, const DsrDestStatus *live);
	DsrSwitch *makeDestToggle(const DsrDestination &dest);
	void requestToggle(const DsrDestination &dest, bool wanted);
	bool confirmToggleWhileLive(const DsrDestination &dest, bool wanted);
	void prunePendingToggles();
	QString elapsedText() const;
	QString protectedBannerText() const;
	QString outputMismatch() const;
	QString summaryText(State state) const;
	void setPill(State state);
	void setBanner(const QString &text, const char *kind, const QString &actionText, std::function<void()> action);

	RelayAuth *auth;
	RelayDestinations *destinations;
	RelayStatus *status;

	/* header */
	QLabel *statusPill;
	QLabel *timerLabel;
	DsrIconButton *gearButton;

	/* banner */
	QLabel *banner;
	QPushButton *bannerAction;
	std::function<void()> bannerActionFn;

	/* content */
	QStackedWidget *stack;
	QWidget *messagePage;
	QLabel *messageLabel;
	QWidget *codeRow;
	QLabel *codeLabel;
	QLabel *urlLabel;
	QPushButton *primaryButton;
	QWidget *listPage;
	QScrollArea *scroll;
	QWidget *listContainer;
	QVBoxLayout *listLayout;
	QLabel *summaryLabel;

	/* footer */
	QWidget *footer;
	DsrIconButton *addButton;
	QLabel *countLabel;
	QPushButton *endButton;

	QTimer *tick;

	State current = State::Checking;
	bool compact = false;
	bool firstRunHandled = false;
	bool lapsed = false;
	bool destOffline = false;
	QString pairingError;
	QString lastEnvironment;
	qint64 localStreamStartMs = 0;
	QDateTime protectedLocalSince;

	/* A destination switch the user has moved but the server has not
	 * confirmed. `enabled` is the value they chose, which the row shows in
	 * place of the stale list value, and `inFlight` holds the switch
	 * disabled while the request is still out. Keyed by destination id so
	 * it survives the row rebuild that every refresh does. */
	struct PendingToggle {
		bool enabled = false;
		bool inFlight = false;
	};
	QHash<QString, PendingToggle> pendingToggles;

	QString targetServer;
	QString targetKey;
	bool targetFetched = false;
	bool targetFetchInFlight = false;
};
