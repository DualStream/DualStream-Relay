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
} // namespace

RelayStatus::RelayStatus(RelayAuth *auth, QObject *parent) : QObject(parent), auth(auth)
{
	timer.setInterval(kPollIntervalMs);
	connect(&timer, &QTimer::timeout, this, &RelayStatus::pollNow);
}

void RelayStatus::setLivePolling(bool enabled)
{
	if (enabled && !timer.isActive())
		timer.start();
	else if (!enabled && timer.isActive())
		timer.stop();
}

void RelayStatus::pollNow()
{
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
	emit updated();

	auth->post(QStringLiteral("/api/relay/sessions/end"), QJsonObject(), [this](const DsrApiResult &result) {
		emit endPosted(result.ok());
		if (!result.ok()) {
			endingFlag = false;
			emit endFinished(false);
			emit updated();
			return;
		}
		/* The flag clears when a later poll shows the session gone. A
		 * safety poll keeps that prompt when the timer is off. */
		pollNow();
	});
}
