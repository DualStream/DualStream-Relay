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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <obs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Twitch dual format through the relay.
 *
 * Twitch dictates a ladder of renditions across the landscape and portrait
 * canvases, the plugin encodes every one of them, and all of them travel in
 * one MPEG-TS contribution over one SRT connection to the relay's landscape
 * ingest. The relay binds renditions by ascending video PID and hands them to
 * Twitch as one Enhanced Broadcasting session.
 *
 * This header is the boundary between the C output that carries the
 * contribution and the C++ side that knows the account, the canvases and the
 * dock. */

#define DSR_LADDER_OUTPUT_ID "dsr_ladder_output"
#define DSR_LADDER_MAX_RENDITIONS 10

struct dsr_ladder_rendition {
	/* 0 = landscape (OBS's own program), 1 = the portrait canvas. */
	int canvas_index;
	uint32_t width;
	uint32_t height;
	uint32_t fps_num;
	uint32_t fps_den;
	int bitrate_kbps;
	int keyint_sec;
	/* Twitch dictates the codec per rung: H.264, or HEVC where it wants
	 * one and the machine offered it. */
	bool hevc;
};

/* One prepared ladder. Rendition order is the contract: index i travels on
 * video PID 0x200 + i and becomes Enhanced RTMP track i. */
struct dsr_ladder_spec {
	char config_id[128];
	struct dsr_ladder_rendition renditions[DSR_LADDER_MAX_RENDITIONS];
	size_t count;
};

/* Whether this build carries the multi-rendition output at all. */
bool dsr_ladder_available(void);

/* Set by the dock from everything it knows: signed in, an enabled Twitch
 * destination on both canvases, the portrait canvas on. The relay service
 * reads it when OBS asks which output to build for the next stream. */
void dsr_ladder_set_wanted(bool wanted);
bool dsr_ladder_wanted(void);

/* Every press of Start Streaming opens a session; a reconnect does not. The
 * output prepares one ladder per session and keeps it across reconnects,
 * and this counter is how it tells the two apart. */
uint64_t dsr_ladder_begin_session(void);
uint64_t dsr_ladder_current_session(void);

/* Credentials for the prepare call, copied. Pushed by the dock whenever they
 * change, so the output's own thread never touches the account object. */
void dsr_ladder_set_auth(const char *api_base, const char *bearer);

/* What preparing a session's ladder came to. The stream starts whatever the
 * answer: a ladder that could not be had, in whole or in part, means less
 * goes to Twitch, never that Start Streaming fails. */
enum dsr_ladder_outcome {
	/* The spec holds the ladder Twitch dictated, both canvases. */
	DSR_LADDER_READY,
	/* The spec holds a desktop-only ladder: Twitch asked for a mobile
	 * picture this machine cannot encode, so it was asked again without
	 * the mobile canvas. */
	DSR_LADDER_DESKTOP_ONLY,
	/* No ladder: OBS's own encoder goes out alone this session. */
	DSR_LADDER_NONE,
	/* The start was called off while asking. */
	DSR_LADDER_ABORTED,
};

/* Ask the website for the ladder. Blocking, meant for the output's start
 * thread; abort_flag ends the wait early. Anything short of READY leaves a
 * note for the dock, in words fit for the streamer, saying what was left
 * out and why. */
enum dsr_ladder_outcome dsr_ladder_prepare(struct dsr_ladder_spec *out, const volatile bool *abort_flag);

/* Take a prepared configuration down again, because the session will not
 * send the ladder after all. Blocking, abortable like the prepare. */
void dsr_ladder_discard(const volatile bool *abort_flag);

/* The note the last prepare left for this session, empty when there is
 * none. Cleared when a session begins. */
size_t dsr_ladder_note(char *buf, size_t len);

/* The encoders behind a ladder: one per rendition, on the right canvas, at
 * the right size, all keyframing together. Attached to the output in ladder
 * order. Freed by detach. */
struct dsr_ladder_encoders;
struct dsr_ladder_encoders *dsr_ladder_attach(obs_output_t *output, const struct dsr_ladder_spec *spec, char *error,
					      size_t error_len);
void dsr_ladder_detach(obs_output_t *output, struct dsr_ladder_encoders *set);

/* Let go of the encoders a finished session left on the stream output.
 * They point at the mobile canvas's video mix, so the canvas calls this
 * before that mix goes. An output that is not the ladder's, or one with a
 * session under way, is left alone. */
void dsr_ladder_output_forget_session(obs_output_t *output);

/* What the output is doing, for the dock. A prepared ladder leaves a
 * configuration on the website that must be removed once the session it
 * was made for is over. A fallback after the ladder was prepared, when its
 * encoders could not be made, is reported the same way a prepare that
 * fell short is. */
void dsr_ladder_report_active(size_t renditions);
void dsr_ladder_report_fallback(const char *reason);
size_t dsr_ladder_active_renditions(void);
void dsr_ladder_note_prepared(bool prepared);
bool dsr_ladder_prepared_pending(void);

#ifdef __cplusplus
}
#endif
