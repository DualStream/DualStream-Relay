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

#include "relay-auth-internal.hpp"
#include "relay-secrets.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

namespace {

const char *kDefaultBase = "https://www.dualstream.gg";

/* Fall back to the legacy JWT lifetime when the server does not say how
 * long the token lives. Refreshing at half-life keeps a margin either way. */
const qint64 kFallbackTokenLifeSec = 7 * 24 * 3600;

} // namespace

QString dsrPickString(const QJsonObject &obj, std::initializer_list<const char *> keys)
{
	for (const char *key : keys) {
		const QJsonValue value = obj.value(QLatin1String(key));
		if (value.isString() && !value.toString().isEmpty())
			return value.toString();
	}
	return QString();
}

qint64 dsrPickInt(const QJsonObject &obj, std::initializer_list<const char *> keys)
{
	for (const char *key : keys) {
		const QJsonValue value = obj.value(QLatin1String(key));
		if (value.isDouble())
			return static_cast<qint64>(value.toDouble());
	}
	return 0;
}

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
	accountEmail = obj.value(QStringLiteral("email")).toString();
	issuedAt = static_cast<qint64>(obj.value(QStringLiteral("issued_at")).toDouble());
	expiresIn = static_cast<qint64>(obj.value(QStringLiteral("expires_in")).toDouble());

	/* Tokens are encrypted where the platform offers a way to do it. A file
	 * written before that, or on a platform without one, still carries them
	 * in the clear; reading those keeps the user signed in, and the next
	 * save puts them away properly. */
	const QString sealed = obj.value(QStringLiteral("tokens")).toString();
	if (!sealed.isEmpty()) {
		const QJsonObject tokens = QJsonDocument::fromJson(dsrSecretUnprotectText(sealed).toUtf8()).object();
		accessToken = tokens.value(QStringLiteral("access_token")).toString();
		refreshValue = tokens.value(QStringLiteral("refresh_token")).toString();
		return;
	}

	accessToken = obj.value(QStringLiteral("access_token")).toString();
	refreshValue = obj.value(QStringLiteral("refresh_token")).toString();
	if (!refreshValue.isEmpty() && dsrSecretsAvailable())
		saveState();
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
	obj.insert(QStringLiteral("email"), accountEmail);
	obj.insert(QStringLiteral("issued_at"), static_cast<double>(issuedAt));
	obj.insert(QStringLiteral("expires_in"), static_cast<double>(expiresIn));

	QJsonObject tokens;
	tokens.insert(QStringLiteral("access_token"), accessToken);
	tokens.insert(QStringLiteral("refresh_token"), refreshValue);

	const QString sealed =
		dsrSecretProtectText(QString::fromUtf8(QJsonDocument(tokens).toJson(QJsonDocument::Compact)));
	if (!sealed.isEmpty()) {
		obj.insert(QStringLiteral("tokens"), sealed);
	} else {
		/* No store on this platform. The refresh token still has to
		 * survive a restart or the user is signed out every launch, so
		 * it goes in as it always has, in a file only their account can
		 * read. */
		obj.insert(QStringLiteral("access_token"), accessToken);
		obj.insert(QStringLiteral("refresh_token"), refreshValue);
	}

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return;
	file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
	file.commit();
}

void RelayAuth::request(const QByteArray &verb, const QString &path, const QJsonObject &body, bool hasBody,
			Handler handler, bool retried)
{
	const QByteArray url = (apiBase() + path).toUtf8();
	const QByteArray payload = hasBody ? QJsonDocument(body).toJson(QJsonDocument::Compact) : QByteArray();
	const QByteArray bearer = accessToken.isEmpty() ? QByteArray() : ("Bearer " + accessToken.toUtf8());

	/* The URL, payload and bearer are captured by value so the worker
	 * never touches members off the UI thread. The completion arrives back
	 * on the UI thread; the QPointer covers this object being destroyed
	 * while the request is in flight. */
	QPointer<RelayAuth> self(this);
	http.send(verb, url, payload, hasBody, bearer,
		  [self, verb, path, body, hasBody, handler, retried](const DsrHttpReply &reply) {
			  if (!self)
				  return;
			  self->finishRequest(reply.status, reply.transportOk, reply.body, verb, path, body, hasBody,
					      handler, retried);
		  });
}

void RelayAuth::finishRequest(int status, bool transportOk, const QByteArray &rawBody, const QByteArray &verb,
			      const QString &path, const QJsonObject &body, bool hasBody, Handler handler, bool retried)
{
	DsrApiResult result;
	result.status = status;
	result.transportOk = transportOk;
	result.body = QJsonDocument::fromJson(rawBody).object();

	if (status == 401 && !retried && !refreshValue.isEmpty()) {
		refreshToken([this, verb, path, body, hasBody, handler, result](bool ok) mutable {
			if (ok) {
				request(verb, path, body, hasBody, handler, true);
				return;
			}
			/* finishRefresh clears the credential only when the
			 * server rejected it outright, so an empty one here is
			 * what distinguishes a finished session from a refresh
			 * that never arrived. */
			result.sessionDead = refreshValue.isEmpty();
			if (handler)
				handler(result);
		});
		return;
	}

	/* A 401 while holding a token and with nothing left to refresh with is
	 * the same finished session, reached without a refresh attempt. */
	if (status == 401 && !accessToken.isEmpty() && refreshValue.isEmpty())
		result.sessionDead = true;

	if (handler)
		handler(result);
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
		dsrPickString(data, {"token", "access_token", "supabase_access_token", "supabaseAccessToken"});
	if (token.isEmpty())
		return false;

	accessToken = token;
	const QString refresh =
		dsrPickString(data, {"refresh_token", "supabase_refresh_token", "supabaseRefreshToken"});
	if (!refresh.isEmpty())
		refreshValue = refresh;
	const QString mail = dsrPickString(data, {"email"});
	if (!mail.isEmpty())
		accountEmail = mail;
	issuedAt = QDateTime::currentSecsSinceEpoch();
	expiresIn = dsrPickInt(data, {"expires_in", "expiresIn"});
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

	/* Bypass request() here: a refresh must never recurse into another
	 * refresh on 401. */
	QJsonObject body;
	body.insert(QStringLiteral("refresh_token"), refreshValue);
	const QByteArray url = (apiBase() + QStringLiteral("/api/auth/refresh")).toUtf8();
	const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

	QPointer<RelayAuth> self(this);
	http.send("POST", url, payload, true, QByteArray(), [self](const DsrHttpReply &reply) {
		if (!self)
			return;
		self->finishRefresh(reply.status, reply.transportOk, reply.body);
	});
}

void RelayAuth::finishRefresh(int status, bool transportOk, const QByteArray &rawBody)
{
	refreshing = false;

	const QJsonObject root = QJsonDocument::fromJson(rawBody).object();

	bool ok = false;
	if (transportOk && status >= 200 && status < 300) {
		QJsonObject data = root.value(QStringLiteral("data")).toObject();
		ok = applyTokens(data.isEmpty() ? root : data);
	} else if (status == 401 || status == 400) {
		/* The refresh credential itself is dead. Keep the access
		 * token until it stops working; the user can re-pair from the
		 * dock when it does. */
		refreshValue.clear();
		saveState();
	}

	const QVector<std::function<void(bool)>> waiters = std::move(refreshWaiters);
	refreshWaiters.clear();
	for (const auto &waiter : waiters) {
		if (waiter)
			waiter(ok);
	}
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

void RelayAuth::signOut()
{
	accessToken.clear();
	refreshValue.clear();
	accountEmail.clear();
	issuedAt = 0;
	expiresIn = 0;
	saveState();
	/* The destinations belonged to the account that just left, so the keys
	 * cached for them do not survive it. */
	dsrSecretForgetAll();
	emit stateChanged();
}

void RelayAuth::sessionExpired()
{
	if (accessToken.isEmpty() && refreshValue.isEmpty())
		return;

	obs_log(LOG_WARNING, "session tokens rejected and not refreshable; sign-in required");
	accessToken.clear();
	refreshValue.clear();
	issuedAt = 0;
	expiresIn = 0;
	saveState();
	emit stateChanged();
}
