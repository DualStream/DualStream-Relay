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
#include "relay-limits.h"

#include <cstring>
#include <mutex>

#include <obs-module.h>

namespace {

std::mutex stateMutex;
bool wantedFlag = false;
bool plansAllowedFlag = false;
QByteArray apiBase;
QByteArray bearer;
size_t activeRenditions = 0;
uint32_t activeTallest = 0;
uint32_t activeShortest = 0;
bool preparedPending = false;
uint64_t sessionCounter = 0;
QByteArray sessionNote;

/* Whether the canvas OBS would send now is above what the relay carries to
 * every platform whole. Read from OBS each time it is asked, so the answer
 * is the canvas as it stands rather than the canvas when something last
 * thought to look. */
bool canvasAbovePlainCeiling()
{
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return false;
	const uint32_t shortSide = ovi.output_width < ovi.output_height ? ovi.output_width : ovi.output_height;
	return shortSide > dsr_limits_get()->landscape.max_height;
}

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

void dsr_ladder_set_dual_format(bool wanted)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	wantedFlag = wanted;
}

bool dsr_ladder_dual_format(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	return wantedFlag;
}

/* Either layout needs the output, and the prepare flow picks between them
 * from the same two answers. */
bool dsr_ladder_wanted(void)
{
	{
		const std::lock_guard<std::mutex> lock(stateMutex);
		if (wantedFlag)
			return true;
	}
	return dsr_ladder_plans_wanted();
}

void dsr_ladder_set_plans_allowed(bool allowed)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	plansAllowedFlag = allowed;
}

bool dsr_ladder_plans_allowed(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	return plansAllowedFlag;
}

bool dsr_ladder_plans_wanted(void)
{
	{
		const std::lock_guard<std::mutex> lock(stateMutex);
		if (!plansAllowedFlag)
			return false;
	}
	/* Asked outside the lock: nothing here is shared state, and OBS is
	 * not somewhere to go while holding a lock the output's thread
	 * wants. */
	return canvasAbovePlainCeiling();
}

/* A new session starts with a clean slate: nothing on air yet, nothing to
 * say about it yet. */
uint64_t dsr_ladder_begin_session(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	activeRenditions = 0;
	activeTallest = 0;
	activeShortest = 0;
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

void dsr_ladder_report_active(size_t renditions, const struct dsr_ladder_spec *spec)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	activeRenditions = renditions;
	activeTallest = 0;
	activeShortest = 0;
	/* Sizes only from the spec these encoders were built from. A count
	 * that disagrees with it is no basis for naming heights. */
	if (!spec || spec->count != renditions)
		return;
	for (size_t i = 0; i < spec->count; i++) {
		const uint32_t height = spec->renditions[i].height;
		if (height == 0)
			continue;
		if (height > activeTallest)
			activeTallest = height;
		if (activeShortest == 0 || height < activeShortest)
			activeShortest = height;
	}
}

bool dsr_ladder_active_heights(uint32_t *tallest, uint32_t *shortest)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	if (activeTallest == 0 || activeTallest == activeShortest)
		return false;
	if (tallest)
		*tallest = activeTallest;
	if (shortest)
		*shortest = activeShortest;
	return true;
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
