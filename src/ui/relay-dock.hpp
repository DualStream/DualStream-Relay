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

#include <functional>

#include <obs-frontend-api.h>

#include "../relay-auth.hpp"
#include "../relay-output.h"
#include "../relay-destinations.hpp"
#include "../relay-status.hpp"
#include "dsr-widgets.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
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

	/* Where the stream is, which decides what may be written to the
	 * streaming service. OBS reports a stream active only once it has
	 * connected, so that flag alone would call the whole connect phase
	 * idle and let the service be replaced out from under the thread
	 * still reading it. */
	enum class StreamPhase {
		/* No output. The whole route may be rewritten. */
		Idle,
		/* The streaming-starting event, before the output has been
		 * handed the service. The key may still be set in place. */
		Starting,
		/* The output owns the service and re-reads it on every
		 * connection attempt. Nothing may be written. */
		Running,
	};

	State computeState() const;
	StreamPhase streamPhase() const;
	void refreshUi();
	void rebuildRows();
	void refreshTick();
	void refreshAll();
	void fetchIngestTarget(std::function<void(bool ok)> done);
	void finishTargetFetch(bool ok);
	void routeToRelay();
	void applyRoute();
	bool routeChangeAllowed();
	void offerEncoderTune();
	QStringList encoderTuneChanges(const dsr_encoder_settings &current) const;
	void restoreRoute();
	void openSettings();
	void openAddDialog();
	void firstRunShow();
	bool keyMismatch() const;
	void repairStreamKey();
	void endEverything();
	QString environmentSignature() const;
	QString blockingSetupIssue() const;
	QString connectedAccountNote() const;
	QString streamStopNotice() const;
	void openEditDialog(const QString &destinationId);

	/* Implemented in relay-dock-direct.cpp. */
	void buildDirectPage();
	void buildDirectSummary(QVBoxLayout *parent);
	void buildDirectForm(QVBoxLayout *parent);
	void fillDirectForm();
	void refreshDirectPage();
	void saveDirectDestination();
	void removeDirectDestination();
	void applyDirectDestination();
	void showDirectStatus(const QString &text, bool ok);
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
	/* Which banner is on screen, so the per-second tick can refresh the
	 * one countdown it owns without overwriting anything that outranked
	 * it. */
	QString bannerKind;

	/* content */
	QStackedWidget *stack;
	QWidget *messagePage;
	QLabel *messageLabel;
	QWidget *codeRow;
	QLabel *codeLabel;
	QLabel *urlLabel;
	QPushButton *primaryButton;
	QWidget *listPage;
	QWidget *directPage = nullptr;
	QLineEdit *directServerEdit = nullptr;
	QLineEdit *directKeyEdit = nullptr;
	QComboBox *directCanvasCombo = nullptr;
	QLabel *directIntro = nullptr;
	QLabel *directHint = nullptr;
	QLabel *directStatus = nullptr;
	QWidget *directSummary = nullptr;
	QLabel *directSummaryName = nullptr;
	QLabel *directSummaryNote = nullptr;
	QLabel *directSummaryCanvas = nullptr;
	QWidget *directForm = nullptr;
	QPushButton *directSaveButton = nullptr;
	QPushButton *directCancelButton = nullptr;
	QPushButton *directRemoveButton = nullptr;
	/* The form is shown while adding or changing, the summary once there is
	 * something to show. */
	bool directEditing = false;
	bool directFormFilled = false;
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

	/* The prepared SRT target the plugin routes to, plus the RTMPS key from
	 * the same response for a profile that sits on the services.json entry.
	 * The fetch time gates staleness: an ingest key can be rotated from the
	 * web at any moment, so a cached target is re-read at every stream
	 * start and whenever it has sat unused long enough to doubt. */
	QString targetServer;
	QString targetKey;
	QString rtmpsServer;
	QString rtmpsKey;
	qint64 targetFetchedAtMs = 0;
	bool targetFetched = false;
	bool targetFetchInFlight = false;

	/* Callbacks waiting on the fetch already in flight. A second request
	 * while one is out joins it rather than being refused: the caller at
	 * stream start needs its repair to run when the answer lands, whoever
	 * asked first. */
	QVector<std::function<void(bool)>> targetFetchWaiters;

	/* True only across the streaming-starting event, the last moment at
	 * which the key can still be set: OBS hands the service to the output
	 * as soon as that event returns. Everything after is read from the
	 * output itself, which is the only thing that knows whether it has
	 * taken the service yet. */
	bool streamStarting = false;

	/* A route request waiting on the ingest target. Holds off a second
	 * one, which would otherwise apply the route and ask about the
	 * encoder once per click. */
	bool routeRequestPending = false;
};
