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

/* Uploading a standby screen takes three calls, because the bytes never pass
 * through the API: it signs a storage url, the file goes straight there, and a
 * last call commits the row. The upload is the only one of the three that
 * leaves the DualStream host, and it carries no session token. */

#include "relay-slate.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>

#include <obs-module.h>
#include <plugin-support.h>

namespace {

/* The one kind disconnect protection plays. The API accepts others that
 * nothing reads. */
const char *kKind = "brb";

const qint64 kMaxBytes = 50LL * 1024 * 1024;

/* A file of this size over a domestic uplink needs minutes, not the seconds an
 * API call is given. */
const long kUploadTimeoutMs = 300000;

/* Extensions the relay renders, each with the content type the API expects for
 * it. Anything else is refused here rather than after the upload. WebP is left
 * out although the API takes it: it is accepted there as a still image, so an
 * animated one uploads, commits, and then shows the house screen instead with
 * nothing to tell the user why. */
struct MediaType {
	const char *suffix;
	const char *contentType;
};

const MediaType kTypes[] = {
	{"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"},
	{"gif", "image/gif"}, {"mp4", "video/mp4"},  {"webm", "video/webm"},
};

} // namespace

RelaySlate::RelaySlate(RelayAuth *auth, QObject *parent) : QObject(parent), auth(auth) {}

qint64 RelaySlate::maxBytes()
{
	return kMaxBytes;
}

QByteArray RelaySlate::contentTypeFor(const QString &filePath)
{
	const QString suffix = QFileInfo(filePath).suffix().toLower();
	for (const MediaType &type : kTypes) {
		if (suffix == QLatin1String(type.suffix))
			return QByteArray(type.contentType);
	}
	return QByteArray();
}

QString RelaySlate::fileFilter()
{
	QString patterns;
	for (const MediaType &type : kTypes)
		patterns += (patterns.isEmpty() ? QStringLiteral("*.") : QStringLiteral(" *.")) +
			    QLatin1String(type.suffix);
	return patterns;
}

void RelaySlate::refresh()
{
	if (!auth->signedIn())
		return;

	QPointer<RelaySlate> self(this);
	auth->get(QStringLiteral("/api/relay/slates"), [self](const DsrApiResult &result) {
		if (!self || !result.ok())
			return;

		/* The list carries every kind the account has; only the one
		 * disconnect protection plays belongs here. */
		self->slate = DsrSlate();
		const QJsonArray rows = result.body.value(QStringLiteral("slates")).toArray();
		for (const QJsonValue &value : rows) {
			const QJsonObject row = value.toObject();
			if (row.value(QStringLiteral("kind")).toString() != QLatin1String(kKind))
				continue;
			self->slate.id = row.value(QStringLiteral("id")).toString();
			self->slate.mediaType = row.value(QStringLiteral("media_type")).toString();
			break;
		}

		self->loadedFlag = true;
		emit self->changed();
	});
}

void RelaySlate::upload(const QString &filePath)
{
	if (busyFlag)
		return;

	const QByteArray contentType = contentTypeFor(filePath);
	if (contentType.isEmpty()) {
		fail(QStringLiteral("Slate.ErrorType"));
		return;
	}

	const QFileInfo info(filePath);
	if (!info.exists() || info.size() <= 0) {
		fail(QStringLiteral("Slate.ErrorUnreadable"));
		return;
	}
	if (info.size() > kMaxBytes) {
		fail(QStringLiteral("Slate.ErrorSize"));
		return;
	}

	busyFlag = true;
	emit changed();
	requestUploadUrl(filePath, contentType);
}

void RelaySlate::requestUploadUrl(const QString &filePath, const QByteArray &contentType)
{
	QJsonObject body;
	body.insert(QStringLiteral("kind"), QLatin1String(kKind));
	body.insert(QStringLiteral("contentType"), QString::fromLatin1(contentType));
	body.insert(QStringLiteral("size"), double(QFileInfo(filePath).size()));

	QPointer<RelaySlate> self(this);
	auth->post(QStringLiteral("/api/relay/slates/upload-url"), body,
		   [self, filePath, contentType](const DsrApiResult &result) {
			   if (!self)
				   return;
			   if (!result.ok()) {
				   self->fail(QStringLiteral("Slate.ErrorUpload"));
				   return;
			   }

			   const QString uploadUrl = result.body.value(QStringLiteral("uploadUrl")).toString();
			   const QString id = result.body.value(QStringLiteral("id")).toString();
			   if (uploadUrl.isEmpty() || id.isEmpty()) {
				   self->fail(QStringLiteral("Slate.ErrorUpload"));
				   return;
			   }

			   /* Read only once the url is in hand, so a refused
			    * request never costs the read. */
			   QFile file(filePath);
			   if (!file.open(QIODevice::ReadOnly)) {
				   self->fail(QStringLiteral("Slate.ErrorUnreadable"));
				   return;
			   }
			   self->putBytes(uploadUrl, id, file.readAll(), contentType);
		   });
}

void RelaySlate::putBytes(const QString &uploadUrl, const QString &id, const QByteArray &bytes,
			  const QByteArray &contentType)
{
	if (bytes.isEmpty()) {
		fail(QStringLiteral("Slate.ErrorUnreadable"));
		return;
	}

	QPointer<RelaySlate> self(this);
	auth->sendUnauthenticated("PUT", uploadUrl.toUtf8(), bytes, contentType, kUploadTimeoutMs,
				  [self, id, contentType](const DsrHttpReply &reply) {
					  if (!self)
						  return;
					  if (!reply.transportOk || reply.status < 200 || reply.status >= 300) {
						  obs_log(LOG_WARNING, "standby screen upload rejected (%d)",
							  reply.status);
						  self->fail(QStringLiteral("Slate.ErrorUpload"));
						  return;
					  }
					  self->commit(id, contentType);
				  });
}

void RelaySlate::commit(const QString &id, const QByteArray &contentType)
{
	QJsonObject body;
	body.insert(QStringLiteral("id"), id);
	body.insert(QStringLiteral("kind"), QLatin1String(kKind));
	body.insert(QStringLiteral("contentType"), QString::fromLatin1(contentType));

	QPointer<RelaySlate> self(this);
	auth->post(QStringLiteral("/api/relay/slates"), body, [self](const DsrApiResult &result) {
		if (!self)
			return;

		self->busyFlag = false;
		if (!result.ok()) {
			self->fail(QStringLiteral("Slate.ErrorUpload"));
			return;
		}

		const QJsonObject row = result.body.value(QStringLiteral("slate")).toObject();
		self->slate.id = row.value(QStringLiteral("id")).toString();
		self->slate.mediaType = row.value(QStringLiteral("media_type")).toString();
		self->loadedFlag = true;
		obs_log(LOG_INFO, "standby screen set");
		emit self->changed();
	});
}

void RelaySlate::remove()
{
	if (busyFlag || slate.isEmpty())
		return;

	busyFlag = true;
	emit changed();

	QPointer<RelaySlate> self(this);
	auth->del(QStringLiteral("/api/relay/slates/%1").arg(slate.id), [self](const DsrApiResult &result) {
		if (!self)
			return;

		self->busyFlag = false;
		if (!result.ok()) {
			self->fail(QStringLiteral("Slate.ErrorRemove"));
			return;
		}

		self->slate = DsrSlate();
		obs_log(LOG_INFO, "standby screen cleared");
		emit self->changed();
	});
}

void RelaySlate::fail(const QString &errorKey)
{
	busyFlag = false;
	emit failed(errorKey);
	emit changed();
}
