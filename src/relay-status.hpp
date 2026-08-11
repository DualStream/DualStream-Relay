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
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include "relay-auth.hpp"

struct DsrDestStatus {
	QString destinationId;
	QString canvas;
	QString state; /* connecting, live, reconnecting, rejected, ended */
	QString lastError;
};

/* Session telemetry over plain HTTPS: GET /api/relay/sessions/current every
 * few seconds while OBS is streaming, nothing while idle. Also owns the
 * end-stream call, which is what turns a Stop Streaming press into an
 * immediate session end instead of five minutes of standby slate. */
class RelayStatus : public QObject {
	Q_OBJECT

public:
	explicit RelayStatus(RelayAuth *auth, QObject *parent = nullptr);

	void setLivePolling(bool enabled);
	void pollNow();
	void requestEnd();

	/* False when the server does not implement the endpoint yet; the dock
	 * then falls back to purely local information. */
	bool endpointAvailable() const { return available; }

	bool reachable() const { return reachableFlag; }
	QDateTime lastSuccessAt() const { return lastSuccess; }

	bool hasSession() const
	{
		return !sessionStatusValue.isEmpty() && sessionStatusValue != QLatin1String("ended");
	}
	QString sessionStatus() const { return sessionStatusValue; }
	QDateTime sessionStartedAt() const { return startedAtValue; }
	QDateTime protectedSince() const { return protectedSinceValue; }
	bool ending() const { return endingFlag; }
	const QVector<DsrDestStatus> &destinations() const { return destStates; }

signals:
	void updated();
	/* The end POST reached the server (or failed). OBS exit waits on this,
	 * bounded, so quitting mid-stream still ends the session. */
	void endPosted(bool ok);
	void endFinished(bool ok);

private:
	void handleFrame(const DsrApiResult &result);

	RelayAuth *auth;
	QTimer timer;
	bool available = true;
	bool reachableFlag = true;
	int failStreak = 0;
	QDateTime lastSuccess;
	QString sessionStatusValue;
	QDateTime startedAtValue;
	QDateTime protectedSinceValue;
	QVector<DsrDestStatus> destStates;
	bool endingFlag = false;
	bool pollInFlight = false;
};
