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

#ifdef __cplusplus
extern "C" {
#endif

/* One SRT caller link to the relay, in live mode, carrying whole MPEG-TS
 * chunks as messages. The server URL is the one the relay minted, as
 * dsr_srt_prepare leaves it: srt://host:port with the latency figures in the
 * query, in microseconds. The stream id names the ingest path and travels as
 * a socket option, never inside the URL.
 *
 * A link exists before it connects, so the owner can break it from another
 * thread at any point of its life: a connect or send blocked on it returns
 * at once with an error. */

struct dsr_srt_link;

struct dsr_srt_link *dsr_srt_link_create(void);

/* Blocking connect. False with the reason in `error`, or at once when the
 * link was broken meanwhile. */
bool dsr_srt_connect(struct dsr_srt_link *link, const char *server_url, const char *stream_id, char *error,
		     size_t error_len);

/* One message. False means the link is gone; the reason is logged. */
bool dsr_srt_send(struct dsr_srt_link *link, const uint8_t *data, size_t len);

/* Break the link from any thread. Safe to call more than once. */
void dsr_srt_abort(struct dsr_srt_link *link);

void dsr_srt_link_destroy(struct dsr_srt_link *link);

#ifdef __cplusplus
}
#endif
