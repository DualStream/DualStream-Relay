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

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSaveFile>
#include <QUrl>

#include <curl/curl.h>

#include <mutex>
#include <string>
#include <thread>

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

namespace {

const char *kDefaultBase = "https://www.dualstream.gg";
const long kConnectTimeoutMs = 5000;
const long kRequestTimeoutMs = 15000;

/* Fall back to the legacy JWT lifetime when the server does not say how
 * long the token lives. Refreshing at half-life keeps a margin either way. */
const qint64 kFallbackTokenLifeSec = 7 * 24 * 3600;

void ensureCurlInit()
{
	/* No matching curl_global_cleanup on purpose: request threads may
	 * still be draining when the module unloads, and OBS keeps libcurl
	 * resident for the life of the process anyway. */
	static std::once_flag once;
	std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t appendBody(char *data, size_t size, size_t nmemb, void *userdata)
{
	auto *out = static_cast<std::string *>(userdata);
	out->append(data, size * nmemb);
	return size * nmemb;
}

struct DsrHttpReply {
	long status = 0;
	bool transportOk = false;
	std::string body;
};

DsrHttpReply runRequest(const QByteArray &verb, const QByteArray &url, const QByteArray &payload, bool hasBody,
			const QByteArray &bearer)
{
	DsrHttpReply reply;

	CURL *curl = curl_easy_init();
	if (!curl)
		return reply;

	struct curl_slist *headers = nullptr;
	headers = curl_slist_append(headers, "Accept: application/json");
	if (hasBody)
		headers = curl_slist_append(headers, "Content-Type: application/json");
	if (!bearer.isEmpty()) {
		headers = curl_slist_append(headers, ("Authorization: " + bearer).constData());
		/* Some deployments strip the Authorization header; the API
		 * reads this fallback header first. */
		headers = curl_slist_append(headers, ("X-Auth-Token: " + bearer).constData());
	}

	const QByteArray userAgent = "obs-dualstream-relay/" + QByteArray(PLUGIN_VERSION);

	curl_easy_setopt(curl, CURLOPT_URL, url.constData());
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.constData());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, kRequestTimeoutMs);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
	/* Required for timeouts on threads: signal-based DNS timeout handling
	 * is not thread safe. */
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	if (verb == "POST") {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, hasBody ? payload.constData() : "");
	} else if (verb != "GET") {
		curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, verb.constData());
		if (hasBody)
			curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, payload.constData());
	}

	const CURLcode result = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &reply.status);
	reply.transportOk = result == CURLE_OK && reply.status > 0;

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
	return reply;
}

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
	ensureCurlInit();
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
	const QByteArray url = (apiBase() + path).toUtf8();
	const QByteArray payload = hasBody ? QJsonDocument(body).toJson(QJsonDocument::Compact) : QByteArray();
	const QByteArray bearer = accessToken.isEmpty() ? QByteArray() : ("Bearer " + accessToken.toUtf8());

	/* One short-lived thread per request; the call volume here is a poll
	 * every few seconds at most. The URL, payload and bearer are captured
	 * by value so the worker never touches members off the UI thread.
	 * Delivery is marshalled through the application object because this
	 * object can be destroyed while a request is in flight; the QPointer
	 * is only dereferenced back on the UI thread. */
	QPointer<RelayAuth> self(this);
	std::thread worker([self, verb, path, body, hasBody, handler, retried, url, payload, bearer]() {
		const DsrHttpReply reply = runRequest(verb, url, payload, hasBody, bearer);
		const QByteArray rawBody = QByteArray(reply.body.data(), int(reply.body.size()));
		const int status = int(reply.status);
		const bool transportOk = reply.transportOk;
		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[self, status, transportOk, rawBody, verb, path, body, hasBody, handler, retried]() {
				if (!self)
					return;
				self->finishRequest(status, transportOk, rawBody, verb, path, body, hasBody, handler,
						    retried);
			},
			Qt::QueuedConnection);
	});
	worker.detach();
}

void RelayAuth::finishRequest(int status, bool transportOk, const QByteArray &rawBody, const QByteArray &verb,
			      const QString &path, const QJsonObject &body, bool hasBody, Handler handler, bool retried)
{
	DsrApiResult result;
	result.status = status;
	result.transportOk = transportOk;
	result.body = QJsonDocument::fromJson(rawBody).object();

	if (status == 401 && !retried && !refreshValue.isEmpty()) {
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
	std::thread worker([self, url, payload]() {
		const DsrHttpReply reply = runRequest("POST", url, payload, true, QByteArray());
		const QByteArray rawBody = QByteArray(reply.body.data(), int(reply.body.size()));
		const int status = int(reply.status);
		const bool transportOk = reply.transportOk;
		QMetaObject::invokeMethod(
			QCoreApplication::instance(),
			[self, status, transportOk, rawBody]() {
				if (!self)
					return;
				self->finishRefresh(status, transportOk, rawBody);
			},
			Qt::QueuedConnection);
	});
	worker.detach();
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
	emit stateChanged();
}
