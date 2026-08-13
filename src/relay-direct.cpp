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

#include "relay-direct.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>

#include "relay-secrets.hpp"

namespace {

QString storePath()
{
	char *path = obs_module_config_path("direct.json");
	if (!path)
		return QString();
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

} // namespace

bool dsrDirectAvailable()
{
	return dsrSecretsAvailable();
}

DsrDirectDestination dsrDirectLoad()
{
	DsrDirectDestination destination;

	const QString path = storePath();
	if (path.isEmpty() || !dsrSecretsAvailable())
		return destination;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return destination;

	const QString sealed = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("d")).toString();
	if (sealed.isEmpty())
		return destination;

	const QJsonObject entry = QJsonDocument::fromJson(dsrSecretUnprotectText(sealed).toUtf8()).object();
	destination.url = entry.value(QStringLiteral("url")).toString();
	destination.key = entry.value(QStringLiteral("key")).toString();
	destination.canvas = entry.value(QStringLiteral("canvas")).toString();
	return destination;
}

bool dsrDirectStore(const DsrDirectDestination &destination)
{
	if (destination.isEmpty()) {
		dsrDirectForget();
		return true;
	}

	const QString path = storePath();
	if (path.isEmpty() || !dsrSecretsAvailable())
		return false;

	QJsonObject entry;
	entry.insert(QStringLiteral("url"), destination.url);
	entry.insert(QStringLiteral("key"), destination.key);
	entry.insert(QStringLiteral("canvas"), destination.canvas);

	const QString sealed =
		dsrSecretProtectText(QString::fromUtf8(QJsonDocument(entry).toJson(QJsonDocument::Compact)));
	if (sealed.isEmpty()) {
		obs_log(LOG_WARNING, "direct destination could not be encrypted for local storage");
		return false;
	}

	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	QJsonObject wrapper;
	wrapper.insert(QStringLiteral("d"), sealed);

	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_WARNING, "direct destination could not be written");
		return false;
	}
	file.write(QJsonDocument(wrapper).toJson(QJsonDocument::Compact));
	return file.commit();
}

void dsrDirectForget()
{
	const QString path = storePath();
	if (path.isEmpty())
		return;
	QFile::remove(path);
}
