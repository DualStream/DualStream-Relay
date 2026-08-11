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

#include "relay-auth.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

namespace {

const char *kDefaultBase = "https://dualstream.gg";
const int kDefaultPollIntervalMs = 5000;
const qint64 kDefaultPairingWindowMs = 15 * 60 * 1000;

/* Fall back to the legacy JWT lifetime when the server does not say how
 * long the token lives. Refreshing at half-life keeps a margin either way. */
const qint64 kFallbackTokenLifeSec = 7 * 24 * 3600;

QString pickString(const QJsonObject &obj, std::initializer_list<const char *> keys)
{
	for (const char *key : keys) {
		const QJsonValue value = obj.value(QLatin1String(key));
		if (value.isString() && !value.toString().isEmpty())
			return value.toString();
	}
	return QString();
}

qint64 pickInt(const QJsonObject &obj, std::initializer_list<const char *> keys)
{
	for (const char *key : keys) {
		const QJsonValue value = obj.value(QLatin1String(key));
		if (value.isDouble())
			return static_cast<qint64>(value.toDouble());
	}
	return 0;
}

} // namespace

RelayAuth::RelayAuth(QObject *parent) : QObject(parent)
{
	pollTimer.setInterval(kDefaultPollIntervalMs);
	connect(&pollTimer, &QTimer::timeout, this, &RelayAuth::pollPairing);
	loadState();
}

QString RelayAuth::apiBase() const
{
	const QString env = qEnvironmentVariable("DSRELAY_API_BASE");
	if (!env.isEmpty())
		return env;
	return QLatin1String(kDefaultBase);
}

QString RelayAuth::webUrl(const QString &path) const
{
	return apiBase() + path;
}

QString RelayAuth::statePath() const
{
	char *path = obs_module_config_path("auth.json");
	if (!path)
		return QString();
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

void RelayAuth::loadState()
{
	const QString path = statePath();
	if (path.isEmpty())
		return;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return;

	const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
	accessToken = obj.value(QStringLiteral("access_token")).toString();
	refreshValue = obj.value(QStringLiteral("refresh_token")).toString();
	accountEmail = obj.value(QStringLiteral("email")).toString();
	issuedAt = static_cast<qint64>(obj.value(QStringLiteral("issued_at")).toDouble());
	expiresIn = static_cast<qint64>(obj.value(QStringLiteral("expires_in")).toDouble());
}

void RelayAuth::saveState()
{
	const QString path = statePath();
	if (path.isEmpty())
		return;

	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	QJsonObject obj;
	obj.insert(QStringLiteral("access_token"), accessToken);
	obj.insert(QStringLiteral("refresh_token"), refreshValue);
	obj.insert(QStringLiteral("email"), accountEmail);
	obj.insert(QStringLiteral("issued_at"), static_cast<double>(issuedAt));
	obj.insert(QStringLiteral("expires_in"), static_cast<double>(expiresIn));

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return;
	file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
	file.commit();
}

void RelayAuth::request(const QByteArray &verb, const QString &path, const QJsonObject &body, bool hasBody,
			Handler handler, bool retried)
{
	QNetworkRequest req(QUrl(apiBase() + path));
	req.setHeader(QNetworkRequest::UserAgentHeader,
		      QStringLiteral("obs-dualstream-relay/%1").arg(QLatin1String(PLUGIN_VERSION)));
	if (hasBody)
		req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	if (!accessToken.isEmpty()) {
		const QByteArray bearer = "Bearer " + accessToken.toUtf8();
		req.setRawHeader("Authorization", bearer);
		/* Some deployments strip the Authorization header; the API
		 * reads this fallback header first. */
		req.setRawHeader("X-Auth-Token", bearer);
	}

	QNetworkReply *reply;
	if (verb == "GET")
		reply = network.get(req);
	else if (hasBody)
		reply = network.sendCustomRequest(req, verb, QJsonDocument(body).toJson(QJsonDocument::Compact));
	else
		reply = network.sendCustomRequest(req, verb);

	connect(reply, &QNetworkReply::finished, this, [this, reply, verb, path, body, hasBody, handler, retried]() {
		reply->deleteLater();

		DsrApiResult result;
		result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		result.transportOk = result.status > 0;
		result.body = QJsonDocument::fromJson(reply->readAll()).object();

		if (result.status == 401 && !retried && !refreshValue.isEmpty()) {
			refreshToken([this, verb, path, body, hasBody, handler, result](bool ok) {
				if (ok)
					request(verb, path, body, hasBody, handler, true);
				else if (handler)
					handler(result);
			});
			return;
		}

		if (handler)
			handler(result);
	});
}

void RelayAuth::get(const QString &path, Handler handler)
{
	request("GET", path, QJsonObject(), false, std::move(handler), false);
}

void RelayAuth::post(const QString &path, const QJsonObject &body, Handler handler)
{
	request("POST", path, body, true, std::move(handler), false);
}

void RelayAuth::patch(const QString &path, const QJsonObject &body, Handler handler)
{
	request("PATCH", path, body, true, std::move(handler), false);
}

void RelayAuth::del(const QString &path, Handler handler)
{
	request("DELETE", path, QJsonObject(), false, std::move(handler), false);
}

bool RelayAuth::applyTokens(const QJsonObject &data)
{
	const QString token =
		pickString(data, {"token", "access_token", "supabase_access_token", "supabaseAccessToken"});
	if (token.isEmpty())
		return false;

	accessToken = token;
	const QString refresh = pickString(data, {"refresh_token", "supabase_refresh_token", "supabaseRefreshToken"});
	if (!refresh.isEmpty())
		refreshValue = refresh;
	const QString mail = pickString(data, {"email"});
	if (!mail.isEmpty())
		accountEmail = mail;
	issuedAt = QDateTime::currentSecsSinceEpoch();
	expiresIn = pickInt(data, {"expires_in", "expiresIn"});
	saveState();
	return true;
}

void RelayAuth::refreshToken(std::function<void(bool)> done)
{
	if (refreshValue.isEmpty()) {
		if (done)
			done(false);
		return;
	}

	refreshWaiters.append(std::move(done));
	if (refreshing)
		return;
	refreshing = true;

	QJsonObject body;
	body.insert(QStringLiteral("refresh_token"), refreshValue);

	/* Bypass request() here: a refresh must never recurse into another
	 * refresh on 401. */
	QNetworkRequest req(QUrl(apiBase() + QStringLiteral("/api/auth/refresh")));
	req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	req.setHeader(QNetworkRequest::UserAgentHeader,
		      QStringLiteral("obs-dualstream-relay/%1").arg(QLatin1String(PLUGIN_VERSION)));

	QNetworkReply *reply = network.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		refreshing = false;

		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();

		bool ok = false;
		if (status >= 200 && status < 300) {
			QJsonObject data = root.value(QStringLiteral("data")).toObject();
			ok = applyTokens(data.isEmpty() ? root : data);
		} else if (status == 401 || status == 400) {
			/* The refresh credential itself is dead. Keep the
			 * access token until it stops working; the user can
			 * re-pair from the dock when it does. */
			refreshValue.clear();
			saveState();
		}

		const QVector<std::function<void(bool)>> waiters = std::move(refreshWaiters);
		refreshWaiters.clear();
		for (const auto &waiter : waiters) {
			if (waiter)
				waiter(ok);
		}
	});
}

void RelayAuth::ensureFreshToken()
{
	if (!signedIn() || refreshing || refreshValue.isEmpty())
		return;

	const qint64 life = expiresIn > 0 ? expiresIn : kFallbackTokenLifeSec;
	const qint64 age = QDateTime::currentSecsSinceEpoch() - issuedAt;
	if (age > life / 2)
		refreshToken(nullptr);
}

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

		deviceCode = pickString(result.body, {"device_code", "deviceCode"});
		userCode = pickString(result.body, {"user_code", "userCode"});
		verifyUrl = pickString(result.body, {"verification_url", "verificationUrl", "verification_uri"});
		if (deviceCode.isEmpty() || userCode.isEmpty()) {
			finishPairing(false, QStringLiteral("Error.PairingUnavailable"));
			return;
		}
		if (verifyUrl.isEmpty())
			verifyUrl = webUrl(QStringLiteral("/link?code=%1").arg(userCode));

		const qint64 interval = pickInt(result.body, {"interval"});
		pollTimer.setInterval(interval > 0 ? int(interval * 1000) : kDefaultPollIntervalMs);
		const qint64 expires = pickInt(result.body, {"expires_in", "expiresIn"});
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

void RelayAuth::signOut()
{
	accessToken.clear();
	refreshValue.clear();
	accountEmail.clear();
	issuedAt = 0;
	expiresIn = 0;
	saveState();
	emit stateChanged();
}
