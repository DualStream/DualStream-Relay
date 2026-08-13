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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host substring that identifies a relay ingest URL. The authoritative
 * server URL comes from the ingest-target API response; this is only used
 * to recognize whether the current profile already points at the relay. */
#define DSR_INGEST_HOST "ingest.dualstream.gg"

/* Name of the entry in OBS's services.json. When the user has selected it,
 * routing keeps them on it and only fills in the key, rather than dropping
 * them back to Custom. That entry can only ever carry RTMPS: OBS resolves its
 * URL from the services file, and the service guidelines do not admit SRT
 * URLs, so an SRT target always ends up on a custom service instead. */
#define DSR_SERVICE_NAME "DualStream Relay"

/* Retransmission window requested on the SRT link, in milliseconds. The
 * desktop app has published to the same relay over SRT with this window since
 * the transport work: round-trip time to the relay swings between 80 and
 * 440 ms, and a window under roughly nine times the peak leaves no room to
 * recover a lost packet before its play deadline. Multistreaming is not
 * interactive, so the base delay this adds costs nothing. */
#define DSR_SRT_LATENCY_MS 4000

/* What the relay wants from the contribution encoder.
 *
 * Bitrate: the relay re-encodes to between 5400 and 6500 kbps, so anything
 * above this is upload spent on detail the second encode discards.
 *
 * Keyframe interval: the relay caps its own GOP at two seconds, and its
 * passthrough mode refuses a stream that goes 8.5 seconds without an IDR.
 * Matching the two-second cadence keeps both happy. */
#define DSR_TARGET_BITRATE_KBPS 6000
#define DSR_TARGET_KEYINT_SEC 2

/* The plugin never owns an output. It points the profile's streaming
 * service at the relay (with the user's consent) and restores the previous
 * service on request. A snapshot of the replaced service is kept in the
 * module config directory so routing is always reversible. */

bool dsr_route_is_relay(void);

/* Returned strings are allocated with bstrdup(); callers bfree() them.
 * NULL when there is no streaming service or no such field. */
char *dsr_route_current_server(void);
char *dsr_route_current_key(void);

/* Turn the SRT URL the relay mints into the two values OBS's stream settings
 * actually take. The relay hands out one URL with the credential inside the
 * query string; OBS wants a bare transport URL in Server and the MediaMTX
 * streamid in Stream Key, and it reads the SRT latency figures in a different
 * unit than the relay writes them. Both outputs are bfree()d by the caller.
 * Returns false when the URL is not an SRT URL or carries no streamid. */
bool dsr_srt_prepare(const char *relay_url, char **server_out, char **stream_id_out);

bool dsr_route_apply(const char *server, const char *key);

/* True when the profile already uses the DualStream Relay services.json
 * entry, whatever key it currently carries. */
bool dsr_route_is_service_entry(void);
bool dsr_route_restore(void);
bool dsr_route_have_snapshot(void);

struct dsr_local_stats {
	bool active;
	int total_frames;
	int dropped_frames;
	float congestion;
};

void dsr_get_local_stats(struct dsr_local_stats *stats);

struct dsr_video_summary {
	uint32_t output_width;
	uint32_t output_height;
	double fps;
};

bool dsr_get_video_summary(struct dsr_video_summary *summary);

/* What the profile's streaming encoder is currently set to.
 *
 * Simple output mode keeps its settings in the profile config and rebuilds the
 * encoder from them at every stream start. Advanced mode keeps them in
 * streamEncoder.json next to the profile and re-reads that file at every
 * stream start. Either way the plugin can change them without OBS needing a
 * restart, and either way the user can still change them back by hand.
 *
 * keyint_sec is -1 when the value is not set or cannot be expressed: simple
 * mode has no keyframe-interval field at all, so only the bitrate is ours to
 * touch there. */
struct dsr_encoder_settings {
	bool advanced;
	int video_bitrate_kbps;
	int keyint_sec;
};

bool dsr_encoder_read(struct dsr_encoder_settings *out);

/* Codec the profile's streaming video encoder produces, as libobs names it
 * ("h264", "hevc", "av1"). NULL in simple output mode, where the stored value
 * is one of OBS's own shorthand names rather than an encoder id and mapping it
 * back would mean copying a table that changes between releases.
 *
 * The relay's contribution contract is H.264. Every encoder profile in the
 * desktop app pins H.264, the relay re-encodes to H.264, and its passthrough
 * mode forwards the contribution to the platforms untouched, where nothing
 * else is broadly playable. Caller bfree()s the result. */
char *dsr_get_stream_video_codec(void);

/* Write the encoder settings back. Pass -1 for keyint_sec to leave it alone.
 * Only bitrate and keyint_sec are ever written: both carry the same name,
 * type and meaning in obs-x264, obs-nvenc, obs-qsv11 and the AMF encoder.
 * Rate control deliberately is not written, because those same four disagree
 * on how its values are spelled. */
bool dsr_encoder_write(int video_bitrate_kbps, int keyint_sec);

/* Simple-output video bitrate in kbps. Returns 0 when the profile uses
 * advanced output mode, where the encoder settings are not readable from
 * the profile config; the preflight note is simply omitted then. */
int dsr_get_configured_bitrate_kbps(void);

/* Name of the streaming service OBS has a connected account for ("Twitch",
 * "YouTube - RTMPS", "Restream.io"), or NULL when none. A connected account
 * supplies its own stream key at go-live, which silently replaces whatever
 * key the service holds, so relay routing cannot work while one is active.
 * Caller bfree()s the result. */
char *dsr_get_connected_account(void);

#ifdef __cplusplus
}
#endif
