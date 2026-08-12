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

/* What the dock is showing and why: the state machine, the status pill, and
 * the checks that decide whether streaming would reach anyone. */

#include "relay-dock.hpp"

#include <QLabel>

#include <obs-module.h>
#include <plugin-support.h>

#include "../relay-output.h"
#include "dsr-ui-common.hpp"

RelayDock::State RelayDock::computeState() const
{
	if (!auth->signedIn())
		return auth->pairing() ? State::Pairing : State::SignedOut;
	if (lapsed)
		return State::Lapsed;
	if (status->ending())
		return State::Ending;
	if (destOffline || !status->reachable())
		return State::Offline;
	if (status->sessionStatus() == QLatin1String("protected"))
		return State::Protected;

	const bool streaming = obs_frontend_streaming_active();
	if (streaming || status->hasSession())
		return State::Live;
	if (!destinations->loaded())
		return State::Checking;
	if (destinations->list().isEmpty())
		return State::Unconfigured;
	if (!dsr_route_is_relay())
		return State::NotRouted;
	return State::Ready;
}

void RelayDock::setPill(State state)
{
	const char *property = "neutral";
	const char *word = "State.Checking";

	switch (state) {
	case State::Checking:
		word = "State.Checking";
		break;
	case State::SignedOut:
		word = "State.SignedOut";
		break;
	case State::Pairing:
		word = "State.Pairing";
		break;
	case State::Lapsed:
		word = "State.Lapsed";
		property = "error";
		break;
	case State::Unconfigured:
		word = "State.Unconfigured";
		break;
	case State::NotRouted:
		word = "State.NotRouted";
		property = "warn";
		break;
	case State::Ready:
		word = "State.Ready";
		property = "ready";
		break;
	case State::Live:
		word = "State.Live";
		property = "live";
		break;
	case State::Protected:
		word = "State.Protected";
		property = "protected";
		break;
	case State::Ending:
		word = "State.Ending";
		break;
	case State::Offline:
		word = "State.Offline";
		property = "warn";
		break;
	}

	statusPill->setText(dsrText(word));
	statusPill->setProperty("state", QLatin1String(property));
	dsrRepolish(statusPill);
}

bool RelayDock::keyMismatch() const
{
	if (!targetFetched || !dsr_route_is_relay())
		return false;

	/* Server and key are checked together. A blank key counts, because OBS
	 * clears it when a connected account is removed and streaming with no
	 * key fails exactly like streaming with a wrong one. The server counts
	 * because a profile set up before this plugin routed over SRT still
	 * points at the RTMPS ingest, which works but is the transport that
	 * falls apart on a lossy uplink. Both are fixed by the same button. */
	char *server = dsr_route_current_server();
	char *key = dsr_route_current_key();
	const bool mismatch = !key || targetKey != QString::fromUtf8(key) || !server ||
			      targetServer != QString::fromUtf8(server);
	bfree(server);
	bfree(key);
	return mismatch;
}

/* Refill our own key when the stream output already points at the relay but
 * carries no key. Nothing of the user's is being replaced in that case, and
 * it is the state OBS leaves behind after disconnecting an account. */
void RelayDock::repairEmptyKey()
{
	if (!targetFetched || !dsr_route_is_relay())
		return;
	char *key = dsr_route_current_key();
	const bool empty = key == NULL;
	bfree(key);
	if (!empty)
		return;

	obs_log(LOG_INFO, "stream key was empty; filling in the relay key");
	dsr_route_apply(targetServer.toUtf8().constData(), targetKey.toUtf8().constData());
}

QString RelayDock::elapsedText() const
{
	qint64 seconds = 0;
	const QDateTime started = status->sessionStartedAt();
	if (started.isValid())
		seconds = started.secsTo(QDateTime::currentDateTimeUtc());
	else if (localStreamStartMs > 0)
		seconds = (QDateTime::currentMSecsSinceEpoch() - localStreamStartMs) / 1000;
	if (seconds < 0)
		seconds = 0;

	return QStringLiteral("%1:%2:%3")
		.arg(seconds / 3600)
		.arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString RelayDock::protectedBannerText() const
{
	QDateTime since = status->protectedSince();
	if (!since.isValid())
		since = protectedLocalSince;

	qint64 remaining = destinations->graceWindowSeconds();
	if (since.isValid())
		remaining -= since.secsTo(QDateTime::currentDateTimeUtc());
	if (remaining < 0)
		remaining = 0;

	const QString countdown =
		QStringLiteral("%1:%2").arg(remaining / 60).arg(remaining % 60, 2, 10, QLatin1Char('0'));

	if (!obs_frontend_streaming_active())
		return QString(dsrText("Protected.Resume")).arg(countdown);
	return QString(dsrText("Protected.Banner")).arg(countdown);
}

/* The one preflight fact worth a banner: the video settings ask for something
 * the relay will not deliver. Everything the old card said about which
 * destinations are enabled is in the rows below it, and the delivered bitrate
 * is not something the user can act on, so neither is repeated here. Empty
 * when the settings and the relay agree, which is the common case. */
QString RelayDock::outputMismatch() const
{
	QStringList issues;

	struct dsr_video_summary video;
	if (dsr_get_video_summary(&video)) {
		const uint32_t shortSide = video.output_width < video.output_height ? video.output_width
										    : video.output_height;
		if (shortSide > 1080)
			issues.append(dsrText("Preflight.Downscale"));
		if (video.fps > 60.5)
			issues.append(dsrText("Preflight.FpsCap"));
	}

	if (dsr_get_configured_bitrate_kbps() > 8000)
		issues.append(dsrText("Preflight.BitrateHigh"));

	return issues.join(QStringLiteral(" "));
}

QString RelayDock::summaryText(State state) const
{
	if (state == State::Live || state == State::Protected) {
		int live = 0;
		int issues = 0;
		for (const DsrDestStatus &dest : status->destinations()) {
			if (dest.state == QLatin1String("live"))
				live++;
			else if (dest.state == QLatin1String("rejected"))
				issues++;
		}
		return QString(dsrText("Summary.Live")).arg(live).arg(issues);
	}
	return statusPill->text();
}

QString RelayDock::environmentSignature() const
{
	char *server = dsr_route_current_server();
	char *key = dsr_route_current_key();
	char *account = dsr_get_connected_account();

	const QString value = QStringLiteral("%1|%2|%3")
				      .arg(QString::fromUtf8(server ? server : ""))
				      .arg(QString::fromUtf8(key ? key : ""))
				      .arg(QString::fromUtf8(account ? account : ""));

	bfree(server);
	bfree(key);
	bfree(account);
	return value;
}

QString RelayDock::blockingSetupIssue() const
{
	if (!dsr_route_is_relay())
		return QString();

	/* A connected account in OBS supplies its own stream key when
	 * streaming starts, replacing the relay key on the way out. The
	 * publish is then refused and OBS retries in a loop, so say exactly
	 * what to do about it. */
	char *account = dsr_get_connected_account();
	if (account) {
		const QString name = QString::fromUtf8(account);
		bfree(account);
		return QString(dsrText("Warning.ConnectedAccount")).arg(name);
	}

	/* The relay takes H.264 and nothing else. OBS will refuse to start on
	 * AV1 by itself, but it will happily send HEVC over SRT, and the relay
	 * has no use for it. Better to say so before the stream starts than to
	 * let it go out and reach no one. */
	char *codec = dsr_get_stream_video_codec();
	if (codec) {
		const QString name = QString::fromUtf8(codec);
		bfree(codec);
		if (name != QLatin1String("h264"))
			return QString(dsrText("Warning.VideoCodec")).arg(name.toUpper());
	}

	return QString();
}
