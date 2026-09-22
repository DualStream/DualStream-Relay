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

/* The account-facing half of dual format: what the dock has decided, the
 * credentials it hands over, and what the output reports back. Everything
 * here may be read from the output's own thread, so the shared state sits
 * behind one lock and never touches Qt objects that belong to the UI
 * thread. The prepare call itself lives in ladder-prepare.cpp. */

#include "ladder.h"
#include "ladder-internal.hpp"

#include <cstring>
#include <mutex>

#include <obs-module.h>

namespace {

std::mutex stateMutex;
bool wantedFlag = false;
QByteArray apiBase;
QByteArray bearer;
size_t activeRenditions = 0;
bool preparedPending = false;
uint64_t sessionCounter = 0;
QByteArray sessionNote;

} // namespace

bool dsr_ladder_available(void)
{
#ifdef DSR_LADDER_OUTPUT
	return true;
#else
	return false;
#endif
}

#ifndef DSR_LADDER_OUTPUT
void dsr_ladder_output_forget_session(obs_output_t *output)
{
	UNUSED_PARAMETER(output);
}
#endif

void dsr_ladder_set_wanted(bool wanted)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	wantedFlag = wanted;
}

bool dsr_ladder_wanted(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	return wantedFlag;
}

/* A new session starts with a clean slate: nothing on air yet, nothing to
 * say about it yet. */
uint64_t dsr_ladder_begin_session(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	activeRenditions = 0;
	sessionNote.clear();
	return ++sessionCounter;
}

uint64_t dsr_ladder_current_session(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	return sessionCounter;
}

void dsr_ladder_set_auth(const char *api_base, const char *bearer_value)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	apiBase = api_base ? QByteArray(api_base) : QByteArray();
	bearer = bearer_value ? QByteArray(bearer_value) : QByteArray();
}

void dsrLadderAuth(QByteArray &base, QByteArray &token)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	base = apiBase;
	token = bearer;
}

void dsrLadderSetNote(const QString &note)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	sessionNote = note.toUtf8();
}

size_t dsr_ladder_note(char *buf, size_t len)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	if (!buf || len == 0)
		return sessionNote.size();
	const size_t copied = (size_t)sessionNote.size() < len - 1 ? (size_t)sessionNote.size() : len - 1;
	memcpy(buf, sessionNote.constData(), copied);
	buf[copied] = '\0';
	return copied;
}

void dsr_ladder_report_fallback(const char *reason)
{
	const QString why = QString::fromUtf8(reason ? reason : "");
	dsrLadderSetNote(QString::fromUtf8(obs_module_text("Ladder.Fallback")).arg(why));
}

void dsr_ladder_report_active(size_t renditions)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	activeRenditions = renditions;
}

size_t dsr_ladder_active_renditions(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	return activeRenditions;
}

void dsrLadderSetPreparedPending(bool pending)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	preparedPending = pending;
}

void dsr_ladder_note_prepared(bool prepared)
{
	dsrLadderSetPreparedPending(prepared);
}

bool dsr_ladder_prepared_pending(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	return preparedPending;
}
