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

/* Pointing OBS at the relay: fetching the ingest target, applying it, and
 * offering to match the encoder to what the relay delivers. */

#include "relay-dock.hpp"

#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>

#include <obs-module.h>
#include <plugin-support.h>

#include "../relay-output.h"
#include "../vertical-canvas.hpp"
#include "dsr-ui-common.hpp"

void RelayDock::fetchIngestTarget(std::function<void(bool)> done)
{
	if (targetFetchInFlight) {
		if (done)
			done(false);
		return;
	}
	targetFetchInFlight = true;

	auth->get(QStringLiteral("/api/relay/ingest-target"), [this, done](const DsrApiResult &result) {
		targetFetchInFlight = false;
		if (!result.ok()) {
			/* This route is gated on entitlement, so its refusal is
			 * the authoritative answer the ungated reads cannot
			 * give. Anything else here is a transport or server
			 * problem the banner already covers. */
			if (result.status == 403 && dsrIsEntitlementCode(result.code())) {
				lapsed = true;
				refreshUi();
			}
			if (done)
				done(false);
			return;
		}

		/* Handing over a target means the account passed the same gate,
		 * which is the only positive proof of entitlement available. */
		lapsed = false;

		const QJsonObject target = result.body.value(QStringLiteral("target")).toObject();
		targetServer.clear();
		targetKey.clear();

		/* SRT first, whatever the response recommends. The relay takes
		 * both, but RTMPS rides TCP: one lost segment stops the window
		 * until it is retransmitted, and on an uplink that drops even a
		 * little the stream collapses rather than degrades. SRT
		 * retransmits inside a window it agrees with the relay up front
		 * and keeps sending in the meantime. */
		const QJsonObject srt = target.value(QStringLiteral("srt")).toObject();
		const QString srtUrl =
			srt.value(QStringLiteral("landscape")).toObject().value(QStringLiteral("url")).toString();
		if (!srtUrl.isEmpty()) {
			char *server = nullptr;
			char *streamId = nullptr;
			if (dsr_srt_prepare(srtUrl.toUtf8().constData(), &server, &streamId)) {
				targetServer = QString::fromUtf8(server);
				targetKey = QString::fromUtf8(streamId);
				bfree(server);
				bfree(streamId);
			}
		}

		/* The portrait canvas publishes to its own ingest path; hand
		 * the prepared target to the vertical side. */
		const QString portraitUrl =
			srt.value(QStringLiteral("portrait")).toObject().value(QStringLiteral("url")).toString();
		if (!portraitUrl.isEmpty() && VerticalCanvas::instance()) {
			char *server = nullptr;
			char *streamId = nullptr;
			if (dsr_srt_prepare(portraitUrl.toUtf8().constData(), &server, &streamId)) {
				VerticalCanvas::instance()->setPortraitTarget(QString::fromUtf8(server),
									      QString::fromUtf8(streamId));
				bfree(server);
				bfree(streamId);
			}
		}

		if (targetServer.isEmpty()) {
			const QJsonObject rtmps = target.value(QStringLiteral("rtmps")).toObject();
			targetServer = rtmps.value(QStringLiteral("server")).toString();
			targetKey = rtmps.value(QStringLiteral("landscape"))
					    .toObject()
					    .value(QStringLiteral("key"))
					    .toString();
		}

		targetFetched = !targetServer.isEmpty() && !targetKey.isEmpty();
		refreshUi();
		if (done)
			done(targetFetched);
	});
}

void RelayDock::routeToRelay()
{
	if (targetFetched) {
		dsr_route_apply(targetServer.toUtf8().constData(), targetKey.toUtf8().constData());
		offerEncoderTune();
		refreshUi();
		return;
	}
	fetchIngestTarget([this](bool ok) {
		if (ok) {
			dsr_route_apply(targetServer.toUtf8().constData(), targetKey.toUtf8().constData());
			offerEncoderTune();
			refreshUi();
		}
	});
}

/* Lines describing what the relay wants changed about the encoder, empty when
 * it is already set up the way the relay wants it. */
QStringList RelayDock::encoderTuneChanges(const dsr_encoder_settings &current) const
{
	QStringList changes;

	/* A profile that has never had its encoder settings edited has no
	 * stored figure to show, so say so rather than printing a zero. */
	const QString unset = dsrText("Tune.Unset");

	if (current.video_bitrate_kbps != DSR_TARGET_BITRATE_KBPS) {
		const int kbps = current.video_bitrate_kbps;
		const QString from = kbps > 0 ? QString::number(kbps) : unset;
		changes.append(QString(dsrText("Tune.Bitrate")).arg(from).arg(DSR_TARGET_BITRATE_KBPS));
	}

	/* Simple output mode has no keyframe-interval setting to change. */
	if (current.advanced && current.keyint_sec != DSR_TARGET_KEYINT_SEC) {
		const QString from = current.keyint_sec >= 0 ? QString::number(current.keyint_sec) : unset;
		changes.append(QString(dsrText("Tune.Keyint")).arg(from).arg(DSR_TARGET_KEYINT_SEC));
	}

	return changes;
}

/* Offer to match the encoder to the relay. This is the part of the plugin the
 * services.json entry cannot do: OBS applies a service's recommended settings
 * only for entries in its own services file, and those entries cannot carry an
 * SRT URL. Routing over SRT therefore means no recommendations arrive at all,
 * and something has to supply them.
 *
 * It asks rather than acting, and it names both numbers, because these are the
 * user's own encoder settings and quietly rewriting them is how a plugin earns
 * a reputation for breaking profiles. */
void RelayDock::offerEncoderTune()
{
	struct dsr_encoder_settings current;
	if (!dsr_encoder_read(&current))
		return;

	const QStringList changes = encoderTuneChanges(current);
	if (changes.isEmpty())
		return;

	QMessageBox box(this);
	box.setWindowTitle(dsrText("Tune.Title"));
	box.setText(dsrText("Tune.Body"));
	box.setInformativeText(changes.join(QStringLiteral("\n")));
	QPushButton *apply = box.addButton(dsrText("Tune.Apply"), QMessageBox::AcceptRole);
	box.addButton(dsrText("Tune.Keep"), QMessageBox::RejectRole);
	box.setDefaultButton(apply);
	box.exec();

	if (box.clickedButton() != apply)
		return;

	dsr_encoder_write(DSR_TARGET_BITRATE_KBPS, current.advanced ? DSR_TARGET_KEYINT_SEC : -1);
}

void RelayDock::restoreRoute()
{
	dsr_route_restore();
	refreshUi();
}
