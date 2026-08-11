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
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

#include "relay-auth.hpp"

struct DsrDestination {
	QString id;
	int slot = 0;
	QString label;
	QString type; /* "oauth" or "custom" */
	QString platform;
	QString customPlatform;
	QString canvas; /* "landscape", "portrait" or "both" */
	bool enabled = true;
	bool hasCustomRtmp = false;
	QJsonObject metadata;
};

struct DsrSuggestion {
	QString platform;
	QString label;
	QString accountId;
	QString username;
	QString suggestedCanvas;
	bool alreadyAdded = false;
};

/* Destination list, limits and the CRUD calls behind the dock and the
 * add/edit dialog. Mid-session changes need no extra call: the relay
 * reconciles running sessions against the enabled set on its own. */
class RelayDestinations : public QObject {
	Q_OBJECT

public:
	explicit RelayDestinations(RelayAuth *auth, QObject *parent = nullptr);

	void refresh();
	void fetchDiscover(std::function<void(bool ok, QVector<DsrSuggestion>)> done);
	void create(const QJsonObject &body, std::function<void(bool ok, QString errorKey)> done);
	void modify(const QString &id, const QJsonObject &patchBody,
		    std::function<void(bool ok, QString errorKey)> done);
	void remove(const QString &id, std::function<void(bool ok, QString errorKey)> done);
	void test(const QString &id, std::function<void(bool ok, bool reachable)> done);

	const QVector<DsrDestination> &list() const { return items; }
	bool loaded() const { return loadedFlag; }
	int enabledCount() const;
	bool hasEnabledTwitch() const;
	int maxDestinations() const { return maxDest; }
	int graceWindowSeconds() const { return graceSeconds; }

	/* Locale key for a server-side error code, so dialogs never surface a
	 * bare code string. */
	static QString errorKeyFor(const DsrApiResult &result);

signals:
	void changed();
	void loadFailed(int status, const QString &code, bool transportOk);

private:
	void fetchLimits();

	RelayAuth *auth;
	QVector<DsrDestination> items;
	bool loadedFlag = false;
	bool limitsFetched = false;
	int maxDest = 8;
	int graceSeconds = 300;
};
