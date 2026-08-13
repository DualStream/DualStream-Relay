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

#include "relay-secrets.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#ifdef _WIN32
#include <windows.h>
#include <dpapi.h>
#endif

namespace {

/* Second factor mixed into the encryption alongside the user account, so a
 * blob lifted out of this file cannot be decrypted by another program running
 * as the same user without also knowing it. */
const char kEntropy[] = "DualStream Relay for OBS stream key store";

QString storePath()
{
	char *path = obs_module_config_path("keys.json");
	if (!path)
		return QString();
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

#ifdef _WIN32

QByteArray protect(const QByteArray &plain)
{
	DATA_BLOB in = {(DWORD)plain.size(), (BYTE *)plain.constData()};
	DATA_BLOB entropy = {(DWORD)(sizeof(kEntropy) - 1), (BYTE *)kEntropy};
	DATA_BLOB out = {0, nullptr};

	if (!CryptProtectData(&in, L"DualStream Relay stream key", &entropy, nullptr, nullptr,
			      CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		obs_log(LOG_WARNING, "stream key could not be encrypted for local storage (error %lu)",
			(unsigned long)GetLastError());
		return QByteArray();
	}

	const QByteArray blob((const char *)out.pbData, (int)out.cbData);
	LocalFree(out.pbData);
	return blob;
}

QByteArray unprotect(const QByteArray &blob)
{
	DATA_BLOB in = {(DWORD)blob.size(), (BYTE *)blob.constData()};
	DATA_BLOB entropy = {(DWORD)(sizeof(kEntropy) - 1), (BYTE *)kEntropy};
	DATA_BLOB out = {0, nullptr};

	if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
		/* Expected after a Windows profile change or a copied config
		 * directory; the key is simply no longer cached here. */
		return QByteArray();
	}

	const QByteArray plain((const char *)out.pbData, (int)out.cbData);
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return plain;
}

const bool kAvailable = true;

#else

/* No secret store wired up for this platform yet. Rather than fall back to
 * writing keys out in the clear, the cache stays empty and every dialog
 * behaves as it did before there was one. */
QByteArray protect(const QByteArray &)
{
	return QByteArray();
}

QByteArray unprotect(const QByteArray &)
{
	return QByteArray();
}

const bool kAvailable = false;

#endif

QJsonObject readStore()
{
	const QString path = storePath();
	if (path.isEmpty())
		return QJsonObject();

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return QJsonObject();
	return QJsonDocument::fromJson(file.readAll()).object();
}

void writeStore(const QJsonObject &obj)
{
	const QString path = storePath();
	if (path.isEmpty())
		return;

	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "stream key store could not be written");
		return;
	}
	file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
	file.commit();
}

} // namespace

bool dsrSecretsAvailable()
{
	return kAvailable;
}

void dsrSecretStore(const QString &destinationId, const DsrRtmpTarget &target)
{
	if (!kAvailable || destinationId.isEmpty())
		return;

	if (target.isEmpty()) {
		dsrSecretForget(destinationId);
		return;
	}

	/* Server and key travel as one encrypted blob. The server is not itself
	 * a secret, but keeping the pair together is what makes a cached
	 * destination usable, and one code path is easier to be sure of. */
	QJsonObject entry;
	entry.insert(QStringLiteral("url"), target.url);
	entry.insert(QStringLiteral("key"), target.key);

	const QByteArray blob = protect(QJsonDocument(entry).toJson(QJsonDocument::Compact));
	if (blob.isEmpty())
		return;

	QJsonObject obj = readStore();
	obj.insert(destinationId, QString::fromLatin1(blob.toBase64()));
	writeStore(obj);
}

DsrRtmpTarget dsrSecretLoad(const QString &destinationId)
{
	DsrRtmpTarget target;
	if (!kAvailable || destinationId.isEmpty())
		return target;

	const QString encoded = readStore().value(destinationId).toString();
	if (encoded.isEmpty())
		return target;

	const QByteArray plain = unprotect(QByteArray::fromBase64(encoded.toLatin1()));
	if (plain.isEmpty())
		return target;

	const QJsonObject entry = QJsonDocument::fromJson(plain).object();
	target.url = entry.value(QStringLiteral("url")).toString();
	target.key = entry.value(QStringLiteral("key")).toString();
	return target;
}

void dsrSecretForget(const QString &destinationId)
{
	if (!kAvailable || destinationId.isEmpty())
		return;

	QJsonObject obj = readStore();
	if (!obj.contains(destinationId))
		return;
	obj.remove(destinationId);
	writeStore(obj);
}

void dsrSecretPrune(const QSet<QString> &keepIds)
{
	if (!kAvailable)
		return;

	QJsonObject obj = readStore();
	bool changed = false;
	for (const QString &key : obj.keys()) {
		if (keepIds.contains(key))
			continue;
		obj.remove(key);
		changed = true;
	}

	if (changed)
		writeStore(obj);
}

QString dsrSecretProtectText(const QString &plain)
{
	if (!kAvailable || plain.isEmpty())
		return QString();

	const QByteArray blob = protect(plain.toUtf8());
	if (blob.isEmpty())
		return QString();
	return QString::fromLatin1(blob.toBase64());
}

QString dsrSecretUnprotectText(const QString &protectedText)
{
	if (!kAvailable || protectedText.isEmpty())
		return QString();

	return QString::fromUtf8(unprotect(QByteArray::fromBase64(protectedText.toLatin1())));
}

void dsrSecretForgetAll()
{
	if (!kAvailable)
		return;
	writeStore(QJsonObject());
}
