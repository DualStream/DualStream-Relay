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

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <functional>

/* Result of one API call. transportOk is false when the request never
 * reached the server at all; the dock renders that as the Offline state
 * instead of treating it like a server-side rejection. */
struct DsrApiResult {
	int status = 0;
	bool transportOk = false;
	QJsonObject body;

	bool ok() const { return transportOk && status >= 200 && status < 300; }
	QString code() const { return body.value(QStringLiteral("code")).toString(); }
};

/* Token store plus the HTTP layer every other component calls through.
 * Sign-in is browser pairing only: the plugin shows a short code, the user
 * approves it at dualstream.gg, and the plugin polls for a token. No
 * password ever passes through this process, and the token lives under the
 * module config directory, never in a scene collection. */
class RelayAuth : public QObject {
	Q_OBJECT

public:
	using Handler = std::function<void(const DsrApiResult &)>;

	explicit RelayAuth(QObject *parent = nullptr);

	bool signedIn() const { return !accessToken.isEmpty(); }
	QString email() const { return accountEmail; }
	QString webUrl(const QString &path) const;

	bool pairing() const { return !deviceCode.isEmpty(); }
	QString pairingCode() const { return userCode; }
	QString pairingUrl() const { return verifyUrl; }

	void startPairing();
	void cancelPairing();
	void signOut();

	/* Refresh ahead of expiry while a stream is running, so the end-stream
	 * call never lands with a stale token. */
	void ensureFreshToken();

	void get(const QString &path, Handler handler);
	void post(const QString &path, const QJsonObject &body, Handler handler);
	void patch(const QString &path, const QJsonObject &body, Handler handler);
	void del(const QString &path, Handler handler);

signals:
	void stateChanged();
	void pairingChanged();
	void pairingFinished(bool ok, const QString &errorKey);

private:
	void request(const QByteArray &verb, const QString &path, const QJsonObject &body, bool hasBody,
		     Handler handler, bool retried);
	void refreshToken(std::function<void(bool)> done);
	bool applyTokens(const QJsonObject &data);
	void loadState();
	void saveState();
	void pollPairing();
	void finishPairing(bool ok, const QString &errorKey);
	QString apiBase() const;
	QString statePath() const;

	QNetworkAccessManager network;

	QString accessToken;
	QString refreshValue;
	QString accountEmail;
	qint64 issuedAt = 0;
	qint64 expiresIn = 0;

	bool refreshing = false;
	QVector<std::function<void(bool)>> refreshWaiters;

	QString deviceCode;
	QString userCode;
	QString verifyUrl;
	QTimer pollTimer;
	qint64 pairingDeadlineMs = 0;
	bool pollInFlight = false;
};
