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

/* Fallback retransmission window for the SRT link, in milliseconds. The
 * relay's SRT front end runs a 2000 ms window: several round trips of
 * headroom at the 80-440 ms RTTs real uplinks show, without the go-live
 * delay and post-stall flush a larger window was measured to add. SRT
 * negotiates the larger of the two sides' figures, so asking for more here
 * would override the relay's choice for every stream from this plugin. A
 * latency figure carried in the minted ingest URL wins over this constant;
 * see dsr_srt_prepare. */
#define DSR_SRT_LATENCY_MS 2000

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
 * module config directory, sealed like every other stored key, so routing
 * is always reversible. */

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

/* Replace only the stream key on the service the profile already uses,
 * leaving the server and therefore the output's transport alone. This is the
 * safe repair while a stream is starting or running: OBS re-reads the service
 * on every connection attempt, so the corrected key is picked up without
 * touching the output. */
bool dsr_route_set_key_inplace(const char *key);

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
 * ("h264", "hevc", "av1"). Advanced mode asks the encoder registry; simple
 * mode reads the codec out of OBS's own shorthand encoder name. NULL when
 * nothing usable is stored.
 *
 * The relay's contribution contract is H.264 video with AAC audio. Its
 * program pipeline parses exactly those two off the ingest; anything else
 * connects fine and then decodes into nothing, leaving every platform on the
 * standby screen. Caller bfree()s the result. */
char *dsr_get_stream_video_codec(void);

/* Codec of the streaming audio track ("aac", "opus"), same contract and
 * ownership as the video variant. */
char *dsr_get_stream_audio_codec(void);

/* Rate control of the streaming video encoder ("CBR", "CQP", ...). Simple
 * mode always builds CBR; advanced mode reads the stored choice. NULL when
 * nothing is stored, which means the encoder's own default. Caller bfree()s
 * the result. */
char *dsr_encoder_rate_control(void);

/* Write the encoder settings back. Pass -1 for keyint_sec to leave it alone.
 * Only bitrate and keyint_sec are ever written: both carry the same name,
 * type and meaning in obs-x264, obs-nvenc, obs-qsv11 and the AMF encoder.
 * Rate control deliberately is not written, because a profile deliberately
 * set to something other than CBR is a choice this plugin only warns about. */
bool dsr_encoder_write(int video_bitrate_kbps, int keyint_sec);

/* Simple-output video bitrate in kbps. Returns 0 when the profile uses
 * advanced output mode, where the encoder settings are not readable from
 * the profile config; the preflight note is simply omitted then. */
int dsr_get_configured_bitrate_kbps(void);

/* Name of the streaming service OBS has a connected account for ("Twitch",
 * "YouTube - RTMPS", "Restream.io"), or NULL when none. A connected account
 * writes its own stream key into the service at every stream start; the
 * repair at the streaming-starting event puts the relay key back. Caller
 * bfree()s the result. */
char *dsr_get_connected_account(void);

/* What the streaming output is doing, taken from its own signals. Arm when a
 * stream starts, disarm when it ends or OBS exits, clear before each start so
 * an old failure does not linger.
 *
 * dsr_stream_output_engaged is true from the moment the output takes the
 * streaming service until it reports being finished with it, the connect
 * attempt included. The service must not be written while it is true. A
 * start that fails before the output takes the service never sets it.
 *
 * dsr_stream_last_stop reports the most recent abnormal stop; the error
 * string is bstrdup()d for the caller and may be NULL. */
void dsr_stream_watch_arm(void);
void dsr_stream_watch_disarm(void);
void dsr_stream_watch_clear(void);
bool dsr_stream_output_engaged(void);
bool dsr_stream_last_stop(int *code, char **error);

#ifdef __cplusplus
}
#endif
