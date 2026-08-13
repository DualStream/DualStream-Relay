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

#include "relay-destinations.hpp"

#include <QJsonArray>
#include <QSet>
#include <QUrl>

#include "relay-secrets.hpp"

namespace {

/* A destination id lands in a URL path. Ids come from our own API and are
 * opaque, so nothing is assumed about their shape rather than trusting them to
 * be path-safe. */
QString pathSegment(const QString &value)
{
	return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

} // namespace

RelayDestinations::RelayDestinations(RelayAuth *auth, QObject *parent) : QObject(parent), auth(auth) {}

void RelayDestinations::refresh()
{
	if (!auth->signedIn())
		return;

	auth->get(QStringLiteral("/api/relay/destinations"), [this](const DsrApiResult &result) {
		if (!result.ok()) {
			emit loadFailed(result.status, result.code(), result.transportOk);
			return;
		}

		items.clear();
		const QJsonArray rows = result.body.value(QStringLiteral("destinations")).toArray();
		for (const QJsonValue &value : rows) {
			const QJsonObject row = value.toObject();
			DsrDestination dest;
			dest.id = row.value(QStringLiteral("id")).toString();
			dest.slot = row.value(QStringLiteral("slot")).toInt();
			dest.label = row.value(QStringLiteral("label")).toString();
			dest.type = row.value(QStringLiteral("type")).toString();
			dest.platform = row.value(QStringLiteral("platform")).toString();
			dest.customPlatform = row.value(QStringLiteral("custom_platform")).toString();
			dest.canvas = row.value(QStringLiteral("canvas")).toString();
			dest.enabled = row.value(QStringLiteral("enabled")).toBool();
			dest.hasCustomRtmp = row.value(QStringLiteral("has_custom_rtmp")).toBool();
			dest.metadata = row.value(QStringLiteral("metadata")).toObject();
			items.append(dest);
		}

		/* A destination removed from another device leaves its cached
		 * stream key behind here, so the authoritative list is what
		 * decides which cached keys are still worth keeping. */
		QSet<QString> live;
		for (const DsrDestination &dest : items)
			live.insert(dest.id);
		dsrSecretPrune(live);

		loadedFlag = true;
		emit changed();
	});

	if (!limitsFetched)
		fetchLimits();
}

void RelayDestinations::fetchLimits()
{
	auth->get(QStringLiteral("/api/relay/limits"), [this](const DsrApiResult &result) {
		if (!result.ok())
			return;
		const QJsonObject limits = result.body.value(QStringLiteral("limits")).toObject();
		const int destinations = limits.value(QStringLiteral("max_destinations")).toInt();
		const int grace = limits.value(QStringLiteral("grace_window_seconds")).toInt();
		if (destinations > 0)
			maxDest = destinations;
		if (grace > 0)
			graceSeconds = grace;
		limitsFetched = true;
		emit changed();
	});
}

void RelayDestinations::fetchDiscover(std::function<void(bool, QVector<DsrSuggestion>)> done)
{
	auth->get(QStringLiteral("/api/relay/discover"), [done](const DsrApiResult &result) {
		QVector<DsrSuggestion> suggestions;
		if (!result.ok()) {
			if (done)
				done(false, suggestions);
			return;
		}

		const QJsonArray rows = result.body.value(QStringLiteral("suggestions")).toArray();
		for (const QJsonValue &value : rows) {
			const QJsonObject row = value.toObject();
			DsrSuggestion suggestion;
			suggestion.platform = row.value(QStringLiteral("platform")).toString();
			suggestion.label = row.value(QStringLiteral("label")).toString();
			suggestion.accountId = row.value(QStringLiteral("connected_account_id")).toString();
			suggestion.username = row.value(QStringLiteral("platform_username")).toString();
			suggestion.suggestedCanvas = row.value(QStringLiteral("suggested_canvas")).toString();
			suggestion.alreadyAdded = row.value(QStringLiteral("already_added")).toBool();
			suggestions.append(suggestion);
		}
		if (done)
			done(true, suggestions);
	});
}

void RelayDestinations::create(const QJsonObject &body, std::function<void(bool, QString, QString)> done)
{
	auth->post(QStringLiteral("/api/relay/destinations"), body, [this, done](const DsrApiResult &result) {
		if (result.ok()) {
			refresh();
			if (done) {
				/* The created row comes back with the response, and
				 * its id is what a caller needs to file the stream
				 * key it just sent under. */
				const QString id = result.body.value(QStringLiteral("destination"))
							   .toObject()
							   .value(QStringLiteral("id"))
							   .toString();
				done(true, QString(), id);
			}
		} else if (done) {
			done(false, failureKey(result), QString());
		}
	});
}

void RelayDestinations::modify(const QString &id, const QJsonObject &patchBody, std::function<void(bool, QString)> done)
{
	auth->patch(QStringLiteral("/api/relay/destinations/%1").arg(pathSegment(id)), patchBody,
		    [this, done](const DsrApiResult &result) {
			    if (result.ok()) {
				    refresh();
				    if (done)
					    done(true, QString());
			    } else if (done) {
				    done(false, failureKey(result));
			    }
		    });
}

void RelayDestinations::remove(const QString &id, std::function<void(bool, QString)> done)
{
	auth->del(QStringLiteral("/api/relay/destinations/%1").arg(pathSegment(id)), [this, done](const DsrApiResult &result) {
		if (result.ok()) {
			refresh();
			if (done)
				done(true, QString());
		} else if (done) {
			done(false, failureKey(result));
		}
	});
}

void RelayDestinations::test(const QString &id, std::function<void(bool, bool)> done)
{
	auth->post(QStringLiteral("/api/relay/destinations/%1/test").arg(pathSegment(id)), QJsonObject(),
		   [done](const DsrApiResult &result) {
			   if (done)
				   done(result.ok(), result.body.value(QStringLiteral("reachable")).toBool());
		   });
}

int RelayDestinations::enabledCount() const
{
	int count = 0;
	for (const DsrDestination &dest : items) {
		if (dest.enabled)
			count++;
	}
	return count;
}

bool RelayDestinations::hasEnabledTwitch() const
{
	for (const DsrDestination &dest : items) {
		if (dest.enabled && dest.platform == QLatin1String("twitch"))
			return true;
	}
	return false;
}

bool dsrIsEntitlementCode(const QString &code)
{
	return code == QLatin1String("ENTITLEMENT_INACTIVE") || code == QLatin1String("SUBSCRIPTION_INACTIVE");
}

QString RelayDestinations::failureKey(const DsrApiResult &result)
{
	if (result.status == 403 && dsrIsEntitlementCode(result.code()))
		emit entitlementInactive();
	return errorKeyFor(result);
}

QString RelayDestinations::errorKeyFor(const DsrApiResult &result)
{
	if (!result.transportOk)
		return QStringLiteral("Error.Network");

	const QString code = result.code();
	if (dsrIsEntitlementCode(code))
		return QStringLiteral("Error.EntitlementInactive");
	if (code == QLatin1String("LIMIT_REACHED"))
		return QStringLiteral("Error.LimitReached");
	if (code == QLatin1String("SLOT_TAKEN"))
		return QStringLiteral("Error.SlotTaken");
	if (code == QLatin1String("MISSING_RTMP"))
		return QStringLiteral("Error.MissingRtmp");
	if (code == QLatin1String("MISSING_ACCOUNT") || code == QLatin1String("ACCOUNT_NOT_FOUND"))
		return QStringLiteral("Error.AccountNotFound");
	if (code == QLatin1String("BAD_SCHEME"))
		return QStringLiteral("Error.BadScheme");
	if (code == QLatin1String("PRIVATE_HOST"))
		return QStringLiteral("Error.PrivateHost");
	if (code == QLatin1String("INVALID_INPUT"))
		return QStringLiteral("Error.InvalidInput");
	return QStringLiteral("Error.Generic");
}
