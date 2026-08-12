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

#include "relay-status.hpp"

#include <QJsonArray>

namespace {
const int kPollIntervalMs = 3000;
const int kOfflineAfterFailures = 2;
/* How long the dock will sit in Ending before giving up on hearing that the
 * session closed. The end call itself has already succeeded by then; what has
 * not arrived is confirmation, and past this point the honest thing to show is
 * the ordinary offline state rather than a spinner that never resolves. */
const qint64 kEndConfirmTimeoutMs = 20000;
} // namespace

RelayStatus::RelayStatus(RelayAuth *auth, QObject *parent) : QObject(parent), auth(auth)
{
	timer.setInterval(kPollIntervalMs);
	connect(&timer, &QTimer::timeout, this, &RelayStatus::pollNow);
}

void RelayStatus::setLivePolling(bool enabled)
{
	liveWanted = enabled;
	updateTimer();
}

/* Polling runs while the stream is live and for as long as an end is still
 * unconfirmed. Ending outlives the stream by design: OBS stops first, and the
 * relay reports the session closed a moment later. Stopping the timer at
 * STREAMING_STOPPED left nothing to notice that it had. */
void RelayStatus::updateTimer()
{
	const bool wanted = liveWanted || endingFlag;
	if (wanted && !timer.isActive())
		timer.start();
	else if (!wanted && timer.isActive())
		timer.stop();
}

void RelayStatus::abandonEnd()
{
	endingFlag = false;
	endDeadlineMs = 0;
	updateTimer();
	emit endFinished(false);
	emit updated();
}

void RelayStatus::pollNow()
{
	if (endingFlag && endDeadlineMs && QDateTime::currentMSecsSinceEpoch() > endDeadlineMs) {
		abandonEnd();
		return;
	}

	if (!auth->signedIn() || pollInFlight)
		return;

	pollInFlight = true;
	auth->get(QStringLiteral("/api/relay/sessions/current"), [this](const DsrApiResult &result) {
		pollInFlight = false;
		handleFrame(result);
	});
}

void RelayStatus::handleFrame(const DsrApiResult &result)
{
	if (!result.transportOk) {
		if (++failStreak >= kOfflineAfterFailures && reachableFlag) {
			reachableFlag = false;
			emit updated();
		}
		return;
	}

	failStreak = 0;
	if (!reachableFlag) {
		reachableFlag = true;
	}

	if (result.status == 404) {
		/* Endpoint not deployed. Keep the dock on local information
		 * only; this is not an outage. */
		available = false;
		lastSuccess = QDateTime::currentDateTime();
		emit updated();
		return;
	}

	if (!result.ok()) {
		emit updated();
		return;
	}

	available = true;
	lastSuccess = QDateTime::currentDateTime();

	const QJsonValue statusValue = result.body.value(QStringLiteral("status"));
	if (!statusValue.isObject()) {
		sessionStatusValue.clear();
		startedAtValue = QDateTime();
		protectedSinceValue = QDateTime();
		destStates.clear();
		if (endingFlag) {
			endingFlag = false;
			endDeadlineMs = 0;
			updateTimer();
			emit endFinished(true);
		}
		emit updated();
		return;
	}

	const QJsonObject frame = statusValue.toObject();
	sessionStatusValue = frame.value(QStringLiteral("status")).toString();
	startedAtValue = QDateTime::fromString(frame.value(QStringLiteral("started_at")).toString(), Qt::ISODate);
	protectedSinceValue =
		QDateTime::fromString(frame.value(QStringLiteral("protected_since")).toString(), Qt::ISODate);

	destStates.clear();
	const QJsonArray rows = frame.value(QStringLiteral("destinations")).toArray();
	for (const QJsonValue &value : rows) {
		const QJsonObject row = value.toObject();
		DsrDestStatus state;
		state.destinationId = row.value(QStringLiteral("destination_id")).toString();
		state.canvas = row.value(QStringLiteral("canvas")).toString();
		state.state = row.value(QStringLiteral("state")).toString();
		state.lastError = row.value(QStringLiteral("last_error")).toString();
		destStates.append(state);
	}

	if (endingFlag && sessionStatusValue == QLatin1String("ended")) {
		endingFlag = false;
		endDeadlineMs = 0;
		updateTimer();
		emit endFinished(true);
	}

	emit updated();
}

void RelayStatus::requestEnd()
{
	if (!auth->signedIn() || endingFlag)
		return;

	/* Set the flag before the call returns: STREAMING_STOPPING arrives
	 * moments before the ingest drops, and the dock needs to render the
	 * Ending state immediately. */
	endingFlag = true;
	endDeadlineMs = QDateTime::currentMSecsSinceEpoch() + kEndConfirmTimeoutMs;
	updateTimer();
	emit updated();

	auth->post(QStringLiteral("/api/relay/sessions/end"), QJsonObject(), [this](const DsrApiResult &result) {
		emit endPosted(result.ok());
		if (!result.ok()) {
			abandonEnd();
			return;
		}
		/* The flag clears when a later poll shows the session gone. A
		 * safety poll keeps that prompt when the timer is off. */
		pollNow();
	});
}
