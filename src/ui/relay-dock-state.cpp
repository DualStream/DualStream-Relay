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

	const bool protectedNow = status->sessionStatus() == QLatin1String("protected");

	/* While OBS is streaming, its own state outranks a status outage.
	 * Offline only means the status API cannot be reached, and that is a
	 * side note next to a stream that is still running: a pill flipping
	 * to Offline mid-stream reads as the stream failing when nothing
	 * about it has changed. The banner carries the outage instead. */
	if (obs_frontend_streaming_active())
		return protectedNow ? State::Protected : State::Live;

	/* OBS is not streaming, so everything left rests on what the relay
	 * last said, and that is only worth believing while the relay can
	 * still be asked. Trusting a cached session through an outage is how
	 * the dock ends up counting a clock on a stream that already died. */
	if (destOffline || !status->reachable())
		return State::Offline;
	if (protectedNow)
		return State::Protected;
	if (status->hasSession())
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

/* Put the stream key right when something outside the plugin has changed it.
 * Two things do: OBS's connected accounts write their own key into the
 * service at every stream start, and the relay mints a fresh ingest key when
 * the account lapses and comes back. Either way the profile ends up pointing
 * at the relay with a key the relay refuses, the publish fails, and OBS
 * retries into a wall with no explanation.
 *
 * What may be written depends on where the stream is, which is why the
 * phase is consulted rather than any single flag. The output's connect
 * thread re-reads the service on every attempt with no lock on either side,
 * and replacing the service object frees the one it holds, so once the
 * output has it nothing here writes: the stream is taken down instead and
 * the stopped handler brings it back on the corrected key. */
RelayDock::StreamPhase RelayDock::streamPhase() const
{
	/* The output is the authority on whether it holds the service, and it
	 * says so from the moment it takes one, connect attempt included. The
	 * frontend's own answer only turns true once a connection succeeded,
	 * so it is kept alongside as a backstop for a stream this dock never
	 * saw begin, never as the primary test. */
	if (dsr_stream_output_engaged() || obs_frontend_streaming_active())
		return StreamPhase::Running;
	if (streamStarting)
		return StreamPhase::Starting;
	return StreamPhase::Idle;
}

void RelayDock::repairStreamKey()
{
	if (!targetFetched || !dsr_route_is_relay())
		return;

	char *serverRaw = dsr_route_current_server();
	char *keyRaw = dsr_route_current_key();
	const QString server = QString::fromUtf8(serverRaw ? serverRaw : "");
	const QString key = QString::fromUtf8(keyRaw ? keyRaw : "");
	bfree(serverRaw);
	bfree(keyRaw);

	/* Identity is the endpoint, not the whole URL: the latency figures in
	 * the query change when the relay retunes its window, and a profile
	 * routed by an older build still carries the old figures. The key is
	 * what publishing stands or falls on. */
	const QString serverBase = server.section(QLatin1Char('?'), 0, 0);
	const QString targetBase = targetServer.section(QLatin1Char('?'), 0, 0);

	/* Which key belongs to the server the profile is on: the SRT streamid
	 * for the prepared SRT target, the plain ingest key for a profile
	 * still publishing over RTMPS. */
	QString wanted;
	if (!targetBase.isEmpty() && serverBase == targetBase)
		wanted = targetKey;
	else if (!rtmpsKey.isEmpty() && server.startsWith(QLatin1String("rtmps://")))
		wanted = rtmpsKey;

	const StreamPhase phase = streamPhase();

	if (wanted.isEmpty()) {
		/* The server names the relay but matches no target this plugin
		 * knows, an older ingest shape. Idle with no key at all, the
		 * full route can be reapplied; that is the state OBS leaves
		 * behind after disconnecting an account. */
		if (phase == StreamPhase::Idle && key.isEmpty()) {
			obs_log(LOG_INFO, "stream key was empty; filling in the relay key");
			dsr_route_apply(targetServer.toUtf8().constData(), targetKey.toUtf8().constData());
		}
		return;
	}

	if (key == wanted) {
		/* Key right, but the service is stale: an older build's latency
		 * figures in the URL, or the generic custom service an earlier
		 * release routed through. Only replaced while idle, because
		 * replacing the service destroys the one the output is holding.
		 * A stream already under way keeps working as it is. */
		if (phase == StreamPhase::Idle && (server != targetServer || dsr_route_needs_upgrade()))
			dsr_route_apply(targetServer.toUtf8().constData(), targetKey.toUtf8().constData());
		return;
	}

	if (phase != StreamPhase::Running) {
		/* Idle, or the starting event before the output has been given
		 * the service. Setting the key in place is safe in both, and
		 * at the starting event it is what the connection then uses. */
		obs_log(LOG_INFO, "stream key no longer matches the relay ingest; putting it back");
		dsr_route_set_key_inplace(wanted.toUtf8().constData());
		return;
	}

	/* The output already holds the service and is publishing a key the
	 * relay refuses. Nothing here can put that right: writing the service
	 * now would change it under the thread reading it, and stopping the
	 * stream on the user's behalf takes a decision that is theirs. The
	 * stream fails on its own, and the banner that follows names the
	 * reason and offers the repair. */
	obs_log(LOG_WARNING, "stream carries a key the relay no longer accepts; it will be refused");
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

/* Cheap fingerprint of everything outside the plugin that changes what the
 * dock should render: where the stream output points, which key it carries,
 * and whether OBS has a connected account. All local reads, no network. */
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
