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

/* Device pairing: request a code, show it, and poll until the browser
 * approves it or the code expires. */

#include "relay-auth.hpp"

#include "relay-auth-internal.hpp"

#include <QDesktopServices>
#include <QJsonObject>
#include <QUrl>

#include <obs-module.h>
#include <plugin-support.h>

void RelayAuth::startPairing()
{
	if (pairing())
		return;

	QJsonObject body;
	body.insert(QStringLiteral("client"), QStringLiteral("obs-dualstream-relay"));
	body.insert(QStringLiteral("client_version"), QLatin1String(PLUGIN_VERSION));

	post(QStringLiteral("/api/auth/device/start"), body, [this](const DsrApiResult &result) {
		if (!result.ok()) {
			finishPairing(false, result.transportOk ? QStringLiteral("Error.PairingUnavailable")
								: QStringLiteral("Error.Network"));
			return;
		}

		deviceCode = dsrPickString(result.body, {"device_code", "deviceCode"});
		userCode = dsrPickString(result.body, {"user_code", "userCode"});
		verifyUrl = dsrPickString(result.body, {"verification_url", "verificationUrl", "verification_uri"});
		if (deviceCode.isEmpty() || userCode.isEmpty()) {
			finishPairing(false, QStringLiteral("Error.PairingUnavailable"));
			return;
		}
		if (verifyUrl.isEmpty())
			verifyUrl = webUrl(QStringLiteral("/link?code=%1").arg(userCode));

		const qint64 interval = dsrPickInt(result.body, {"interval"});
		pollTimer.setInterval(interval > 0 ? int(interval * 1000) : kDefaultPollIntervalMs);
		const qint64 expires = dsrPickInt(result.body, {"expires_in", "expiresIn"});
		pairingDeadlineMs =
			QDateTime::currentMSecsSinceEpoch() + (expires > 0 ? expires * 1000 : kDefaultPairingWindowMs);

		pollTimer.start();
		QDesktopServices::openUrl(QUrl(verifyUrl));
		emit pairingChanged();
	});
}

void RelayAuth::pollPairing()
{
	if (deviceCode.isEmpty() || pollInFlight)
		return;

	if (QDateTime::currentMSecsSinceEpoch() > pairingDeadlineMs) {
		finishPairing(false, QStringLiteral("Error.PairingExpired"));
		return;
	}

	pollInFlight = true;
	QJsonObject body;
	body.insert(QStringLiteral("device_code"), deviceCode);

	post(QStringLiteral("/api/auth/device/poll"), body, [this](const DsrApiResult &result) {
		pollInFlight = false;
		if (deviceCode.isEmpty())
			return; /* canceled while the poll was in flight */

		if (result.ok()) {
			QJsonObject data = result.body.value(QStringLiteral("data")).toObject();
			if (applyTokens(data.isEmpty() ? result.body : data)) {
				finishPairing(true, QString());
				emit stateChanged();
			}
			return;
		}

		const QString code = result.code();
		if (code == QLatin1String("authorization_pending") || code == QLatin1String("pending") ||
		    result.status == 428 || result.status == 202)
			return; /* keep polling */
		if (code == QLatin1String("slow_down")) {
			pollTimer.setInterval(pollTimer.interval() + 5000);
			return;
		}
		if (!result.transportOk)
			return; /* transient network trouble; keep polling */

		finishPairing(false, result.status == 410 ? QStringLiteral("Error.PairingExpired")
							  : QStringLiteral("Error.PairingDenied"));
	});
}

void RelayAuth::finishPairing(bool ok, const QString &errorKey)
{
	pollTimer.stop();
	deviceCode.clear();
	userCode.clear();
	verifyUrl.clear();
	pollInFlight = false;
	emit pairingChanged();
	emit pairingFinished(ok, errorKey);
}

void RelayAuth::cancelPairing()
{
	if (!pairing())
		return;
	pollTimer.stop();
	deviceCode.clear();
	userCode.clear();
	verifyUrl.clear();
	pollInFlight = false;
	emit pairingChanged();
}
