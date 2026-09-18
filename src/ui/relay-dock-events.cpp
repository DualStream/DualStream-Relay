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

/* How the dock reacts to what happens around it: OBS's frontend events, the
 * end-stream hotkey, and clicks on the destination rows. */

#include "relay-dock.hpp"

#include <QEventLoop>
#include <QMetaObject>
#include <QMouseEvent>
#include <QTimer>

#include <obs-module.h>
#include <plugin-support.h>

#include "../ladder.h"
#include "../relay-output.h"
#include "../relay-profile.h"
#include "dsr-ui-common.hpp"
#include "relay-dock-text.hpp"

void RelayDock::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		/* Normally cleared at module load, before OBS built this
		 * profile's outputs; done again here for a profile that could
		 * not be read that early, with the restart note that entails. */
		if (dsr_route_is_relay())
			dsr_profile_clear_enhanced_broadcasting(true);
		firstRunShow();
		refreshAll();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		auth->ensureFreshToken();
		pushLadderAuth();
		dsr_ladder_begin_session();
		dsr_stream_watch_clear();
		dsr_stream_watch_arm();
		/* True only across this handler. OBS hands the service to the
		 * output the moment the event returns, so this is the last
		 * point at which writing the key is still safe. */
		streamStarting = true;
		/* OBS applies a connected account's own stream key to the
		 * service between the Start press and the output starting,
		 * silently replacing the relay key. This event lands after
		 * that write and before the connection opens, so it is the one
		 * moment the key can be put right for this stream. */
		repairStreamKey();
		/* The ingest key can have been rotated since it was last
		 * fetched, from the web or from another install. This answer
		 * arrives after the output has the service, so it cannot
		 * correct this stream; it makes the next one right and lets
		 * the dock say what went wrong with this one. */
		fetchIngestTarget([this](bool ok) {
			if (ok)
				repairStreamKey();
		});
		streamStarting = false;
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		dsr_stream_watch_arm();
		localStreamStartMs = QDateTime::currentMSecsSinceEpoch();
		status->setLivePolling(true);
		status->pollNow();
		refreshUi();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		/* The single most important call in the plugin: without it the
		 * relay reads a stop as a connection drop and holds every
		 * platform on the standby slate for the grace window. */
		if (auth->signedIn())
			status->requestEnd();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		dsr_stream_watch_disarm();
		streamStarting = false;
		localStreamStartMs = 0;
		status->setLivePolling(false);
		/* Backstop for a missed STOPPING event, and the poll that
		 * clears the Ending state. */
		if (auth->signedIn() && status->hasSession() && !status->ending())
			status->requestEnd();
		/* requestEnd polls as soon as the end call is acknowledged, so
		 * nothing needs to wait a fixed interval for confirmation. */
		refreshUi();
		/* A ladder prepared for this stream stays on file for hours,
		 * and the relay would build the next session around it even
		 * if that session carried a single rendition. It is removed
		 * the moment the stream it was made for is over. */
		if (dsr_ladder_prepared_pending() && auth->signedIn()) {
			dsr_ladder_note_prepared(false);
			auth->del(QStringLiteral("/api/relay/dual-format/prepare"), [](const DsrApiResult &result) {
				if (!result.ok())
					obs_log(LOG_WARNING, "prepared dual format ladder could not be removed (%d)",
						result.status);
			});
		}
		/* Queued rather than called here. This event is delivered from
		 * inside the output's own teardown, which is still reading the
		 * service's strings; writing the service now would pull them
		 * out from under the rest of that unwind. Once it has finished
		 * the service is free to be put right for the next stream. */
		QMetaObject::invokeMethod(
			this,
			[this]() {
				repairStreamKey();
				refreshUi();
			},
			Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		/* OBS has already rebuilt its outputs for the new profile by
		 * the time this lands: any restart the old profile was waiting
		 * on has happened in effect, and a switch found on here is
		 * cleared for the next launch with a fresh restart note. */
		dsr_profile_restart_satisfied();
		if (dsr_route_is_relay())
			dsr_profile_clear_enhanced_broadcasting(true);
		refreshUi();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		dsr_stream_watch_disarm();
		streamStarting = false;
		if (auth->signedIn() && obs_frontend_streaming_active() && !status->ending())
			status->requestEnd();
		if (status->ending()) {
			/* Give the end call a bounded window to reach the
			 * server before the process goes away. */
			QEventLoop loop;
			QTimer::singleShot(1500, &loop, &QEventLoop::quit);
			connect(status, &RelayStatus::endPosted, &loop, &QEventLoop::quit);
			loop.exec();
		}
		break;
	default:
		break;
	}
}

/* End the broadcast from the dock. OBS can still believe it is connected
 * while the relay has fallen back to the standby screen, so stop the output
 * first and let the streaming-stopping handler end the session in the usual
 * order; only end directly when OBS has already stopped. */
void RelayDock::endEverything()
{
	if (obs_frontend_streaming_active()) {
		obs_frontend_streaming_stop();
		return;
	}
	status->requestEnd();
}

void RelayDock::endStreamHotkey()
{
	if (current == State::Live || current == State::Protected)
		endEverything();
}

void RelayDock::firstRunShow()
{
	if (firstRunHandled)
		return;
	firstRunHandled = true;

	if (dsrReadFlag(kFirstRunFlag))
		return;
	dsrWriteFlag(kFirstRunFlag, true);
	showDockWindow();
}

bool RelayDock::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() == QEvent::MouseButtonRelease) {
		QWidget *row = qobject_cast<QWidget *>(watched);
		if (row) {
			const QVariant id = row->property("destId");
			QMouseEvent *me = static_cast<QMouseEvent *>(event);
			if (id.isValid() && me->button() == Qt::LeftButton &&
			    row->rect().contains(me->position().toPoint())) {
				openEditDialog(id.toString());
				return true;
			}
		}
	}
	return QWidget::eventFilter(watched, event);
}
