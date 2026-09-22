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

#include "ladder-ts-internal.h"

#include <string.h>

#include <obs-module.h>
#include <plugin-support.h>

#define PES_STREAM_VIDEO 0xE0
#define PES_STREAM_AUDIO 0xC0

/* The NAL unit types that matter here, in each codec's numbering. */
#define H264_NAL_SPS 7
#define H264_NAL_AUD 9
#define HEVC_NAL_SPS 33
#define HEVC_NAL_AUD 35

/* Receivers expect the program tables at least ten times a second. */
#define PSI_INTERVAL_90K 9000
/* The clock reference runs 300 ms behind the decode times it schedules, so
 * every access unit arrives before the moment it is due. */
#define PCR_LEAD_90K 27000
/* Room for the longest PES header this writer emits. */
#define PES_HEADER_MAX 19

static const uint8_t kH264Delimiter[] = {0x00, 0x00, 0x00, 0x01, 0x09, 0xF0};
static const uint8_t kHevcDelimiter[] = {0x00, 0x00, 0x00, 0x01, 0x46, 0x01, 0x50};

bool dsr_ts_emit_packet(struct dsr_ts_mux *mux, const uint8_t *packet)
{
	if (mux->failed)
		return false;
	memcpy(mux->chunk + mux->chunk_len, packet, DSR_TS_PACKET_SIZE);
	mux->chunk_len += DSR_TS_PACKET_SIZE;
	if (mux->chunk_len < DSR_TS_CHUNK_SIZE)
		return true;

	mux->chunk_len = 0;
	if (!mux->sink(mux->ctx, mux->chunk, DSR_TS_CHUNK_SIZE)) {
		mux->failed = true;
		return false;
	}
	return true;
}

/* Tables go out on the cadence, and again right before any primary keyframe
 * so a receiver joining there has them at once. */
static bool maybe_write_tables(struct dsr_ts_mux *mux, int64_t now, bool force)
{
	if (mux->tables_written && !force && now - mux->tables_at < PSI_INTERVAL_90K)
		return true;
	mux->tables_written = true;
	mux->tables_at = now;
	return dsr_ts_write_tables(mux);
}

static void write_timestamp(uint8_t *out, uint8_t prefix, int64_t ts)
{
	const uint64_t value = (uint64_t)ts & 0x1FFFFFFFFull;
	out[0] = prefix | (uint8_t)(((value >> 30) & 0x07) << 1) | 0x01;
	out[1] = (uint8_t)(value >> 22);
	out[2] = (uint8_t)(((value >> 15) & 0x7F) << 1) | 0x01;
	out[3] = (uint8_t)(value >> 7);
	out[4] = (uint8_t)((value & 0x7F) << 1) | 0x01;
}

static size_t write_pes_header(uint8_t *out, uint8_t stream_id, size_t payload_len, int64_t pts, int64_t dts,
			       bool with_dts, bool bounded)
{
	const size_t header_data = with_dts ? 10 : 5;
	size_t pes_len = bounded ? 3 + header_data + payload_len : 0;
	if (pes_len > 0xFFFF)
		pes_len = 0;

	out[0] = 0x00;
	out[1] = 0x00;
	out[2] = 0x01;
	out[3] = stream_id;
	out[4] = (uint8_t)(pes_len >> 8);
	out[5] = (uint8_t)pes_len;
	out[6] = 0x84;
	out[7] = with_dts ? 0xC0 : 0x80;
	out[8] = (uint8_t)header_data;
	write_timestamp(out + 9, with_dts ? 0x30 : 0x20, pts);
	if (with_dts)
		write_timestamp(out + 14, 0x10, dts);
	return 9 + header_data;
}

static void write_pcr(uint8_t *out, int64_t pcr)
{
	const uint64_t base = (uint64_t)pcr & 0x1FFFFFFFFull;
	out[0] = (uint8_t)(base >> 25);
	out[1] = (uint8_t)(base >> 17);
	out[2] = (uint8_t)(base >> 9);
	out[3] = (uint8_t)(base >> 1);
	out[4] = (uint8_t)(((base & 1) << 7) | 0x7E);
	out[5] = 0x00;
}

/* Split one PES packet into transport packets. The first carries the clock
 * reference when asked; the last is padded with an adaptation field so
 * nothing from the next access unit shares a packet with this one. */
static bool write_pes(struct dsr_ts_mux *mux, struct dsr_ts_stream *stream, const uint8_t *pes, size_t len,
		      bool with_pcr, int64_t pcr)
{
	size_t offset = 0;
	bool first = true;

	while (first || offset < len) {
		uint8_t packet[DSR_TS_PACKET_SIZE];
		const size_t remaining = len - offset;
		const bool pcr_here = first && with_pcr;
		const size_t space = pcr_here ? 176 : 184;
		const size_t take = remaining < space ? remaining : space;
		const size_t adaptation = 184 - take;

		packet[0] = 0x47;
		packet[1] = (first ? 0x40 : 0x00) | (uint8_t)(stream->pid >> 8);
		packet[2] = (uint8_t)stream->pid;
		packet[3] = (adaptation ? 0x30 : 0x10) | (stream->continuity & 0x0F);
		stream->continuity++;

		size_t at = 4;
		if (adaptation) {
			packet[at++] = (uint8_t)(adaptation - 1);
			if (adaptation > 1) {
				packet[at++] = pcr_here ? 0x10 : 0x00;
				if (pcr_here) {
					write_pcr(packet + at, pcr);
					at += 6;
				}
				while (at < 4 + adaptation)
					packet[at++] = 0xFF;
			}
		}
		memcpy(packet + at, pes + offset, take);
		offset += take;
		first = false;

		if (!dsr_ts_emit_packet(mux, packet))
			return false;
	}
	return true;
}

static bool ensure_scratch(struct dsr_ts_mux *mux, size_t size)
{
	if (mux->scratch_size >= size)
		return true;
	uint8_t *grown = brealloc(mux->scratch, size);
	if (!grown)
		return false;
	mux->scratch = grown;
	mux->scratch_size = size;
	return true;
}

/* Position of the start code prefix at or after `from`, or size. */
static size_t find_start_code(const uint8_t *data, size_t size, size_t from)
{
	for (size_t i = from; i + 2 < size; i++) {
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
			return i;
	}
	return size;
}

/* H.264 keeps the type in the low five bits of a one byte header; HEVC in
 * the six bits after the forbidden bit of a two byte one. */
static int nal_type_at(const uint8_t *data, size_t size, size_t start_code, bool hevc)
{
	const size_t header = start_code + 3;
	if (header >= size)
		return -1;
	return hevc ? ((data[header] >> 1) & 0x3F) : (data[header] & 0x1F);
}

static bool contains_nal(const uint8_t *data, size_t size, int type, bool hevc)
{
	size_t at = find_start_code(data, size, 0);
	while (at < size) {
		if (nal_type_at(data, size, at, hevc) == type)
			return true;
		at = find_start_code(data, size, at + 3);
	}
	return false;
}

/* Where the first NAL unit ends: the byte where the next start code prefix
 * begins, its leading zero byte included. */
static size_t first_nal_end(const uint8_t *data, size_t size)
{
	const size_t first = find_start_code(data, size, 0);
	if (first >= size)
		return size;
	size_t next = find_start_code(data, size, first + 3);
	if (next < size && next > 0 && data[next - 1] == 0)
		next--;
	return next;
}

static bool starts_with_start_code(const uint8_t *data, size_t size)
{
	return size > 4 && find_start_code(data, size, 0) < 4;
}

struct dsr_ts_mux *dsr_ts_mux_create(dsr_ts_sink_fn sink, void *ctx)
{
	struct dsr_ts_mux *mux = bzalloc(sizeof(*mux));
	mux->sink = sink;
	mux->ctx = ctx;
	mux->audio.pid = DSR_TS_PID_AUDIO;
	return mux;
}

void dsr_ts_mux_destroy(struct dsr_ts_mux *mux)
{
	if (!mux)
		return;
	for (size_t i = 0; i < mux->video_count; i++)
		bfree(mux->video_headers[i]);
	bfree(mux->scratch);
	bfree(mux);
}

bool dsr_ts_mux_add_video(struct dsr_ts_mux *mux, const uint8_t *parameter_sets, size_t size, bool hevc)
{
	if (mux->video_count >= DSR_TS_MAX_VIDEO)
		return false;

	const size_t index = mux->video_count;
	mux->video[index].pid = DSR_TS_PID_VIDEO_BASE + (uint16_t)index;
	mux->video[index].continuity = 0;
	mux->video_hevc[index] = hevc;
	/* Only a header in byte-stream form can be spliced in front of a
	 * keyframe; anything else is left to the repeated in-band headers,
	 * and said so, because a keyframe with neither is dropped by the
	 * relay with nothing else pointing at the cause. */
	if (parameter_sets && starts_with_start_code(parameter_sets, size)) {
		mux->video_headers[index] = bmemdup(parameter_sets, size);
		mux->video_header_sizes[index] = size;
	} else {
		obs_log(LOG_WARNING,
			"video rendition %zu handed no byte-stream parameter sets; keyframes rely on "
			"the encoder repeating its own",
			index);
	}
	mux->video_count++;
	return true;
}

static uint8_t rate_index_for(uint32_t sample_rate)
{
	static const uint32_t rates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
					 22050, 16000, 12000, 11025, 8000,  7350};
	for (uint8_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
		if (rates[i] == sample_rate)
			return i;
	}
	return 3;
}

bool dsr_ts_mux_set_audio(struct dsr_ts_mux *mux, const uint8_t *config, size_t size, uint32_t sample_rate,
			  size_t channels)
{
	/* The ADTS header has three bits for the channel layout; a
	 * configuration beyond them cannot be described in it and falls back
	 * to what the mix reports. */
	if (config && size >= 2 && ((config[1] >> 3) & 0x0F) <= 7) {
		mux->adts_profile = (uint8_t)(config[0] >> 3);
		mux->adts_rate_index = (uint8_t)(((config[0] & 0x07) << 1) | (config[1] >> 7));
		mux->adts_channels = (uint8_t)((config[1] >> 3) & 0x07);
	} else {
		/* AAC-LC, which is what every OBS audio encoder produces. */
		mux->adts_profile = 2;
		mux->adts_rate_index = rate_index_for(sample_rate);
		mux->adts_channels = (uint8_t)(channels > 7 ? 7 : channels);
	}
	if (mux->adts_profile == 0 || mux->adts_profile > 4)
		mux->adts_profile = 2;
	mux->have_audio = true;
	return true;
}

bool dsr_ts_mux_write_video(struct dsr_ts_mux *mux, size_t index, const uint8_t *data, size_t size, int64_t pts,
			    int64_t dts, bool keyframe)
{
	if (mux->failed || index >= mux->video_count || size == 0)
		return false;

	const bool primary = index == 0;
	if (!maybe_write_tables(mux, dts, primary && keyframe))
		return false;

	/* Every access unit opens with a delimiter, and a keyframe carries its
	 * parameter sets: the ones the encoder wrote, or the encoder's header
	 * spliced in behind the delimiter when it wrote none. */
	const bool hevc = mux->video_hevc[index];
	const int aud_type = hevc ? HEVC_NAL_AUD : H264_NAL_AUD;
	const int sps_type = hevc ? HEVC_NAL_SPS : H264_NAL_SPS;
	const uint8_t *delimiter = hevc ? kHevcDelimiter : kH264Delimiter;
	const size_t delimiter_size = hevc ? sizeof(kHevcDelimiter) : sizeof(kH264Delimiter);
	const size_t first_code = find_start_code(data, size, 0);
	const bool has_delimiter = first_code < size && nal_type_at(data, size, first_code, hevc) == aud_type;
	const bool needs_header = keyframe && !contains_nal(data, size, sps_type, hevc);
	const uint8_t *header = needs_header ? mux->video_headers[index] : NULL;
	const size_t header_size = header ? mux->video_header_sizes[index] : 0;
	if (needs_header && !header && !mux->video_header_missing_noted[index]) {
		mux->video_header_missing_noted[index] = true;
		obs_log(LOG_WARNING, "video rendition %zu sends keyframes without parameter sets; the relay drops them",
			index);
	}

	const size_t payload = size + (has_delimiter ? 0 : delimiter_size) + header_size;
	if (!ensure_scratch(mux, PES_HEADER_MAX + payload))
		return false;

	uint8_t *pes = mux->scratch;
	size_t at = write_pes_header(pes, PES_STREAM_VIDEO, payload, pts, dts, pts != dts, false);
	size_t consumed = 0;

	if (has_delimiter) {
		const size_t delimiter_len = first_nal_end(data, size);
		memcpy(pes + at, data, delimiter_len);
		at += delimiter_len;
		consumed = delimiter_len;
	} else {
		memcpy(pes + at, delimiter, delimiter_size);
		at += delimiter_size;
	}
	if (header) {
		memcpy(pes + at, header, header_size);
		at += header_size;
	}
	memcpy(pes + at, data + consumed, size - consumed);
	at += size - consumed;

	return write_pes(mux, &mux->video[index], pes, at, primary, dts - PCR_LEAD_90K);
}

bool dsr_ts_mux_write_audio(struct dsr_ts_mux *mux, const uint8_t *data, size_t size, int64_t pts)
{
	if (mux->failed || !mux->have_audio || size == 0)
		return false;
	if (!maybe_write_tables(mux, pts, false))
		return false;

	/* Raw AAC frames become ADTS frames: a seven byte header carrying the
	 * profile, rate and channel layout the decoder cannot recover from the
	 * frame itself. */
	const size_t frame_len = 7 + size;
	if (frame_len > 0x1FFF || !ensure_scratch(mux, PES_HEADER_MAX + frame_len))
		return false;

	uint8_t *pes = mux->scratch;
	size_t at = write_pes_header(pes, PES_STREAM_AUDIO, frame_len, pts, pts, false, true);
	uint8_t *adts = pes + at;
	adts[0] = 0xFF;
	adts[1] = 0xF1;
	adts[2] = (uint8_t)(((mux->adts_profile - 1) << 6) | (mux->adts_rate_index << 2) | (mux->adts_channels >> 2));
	adts[3] = (uint8_t)(((mux->adts_channels & 0x03) << 6) | (frame_len >> 11));
	adts[4] = (uint8_t)(frame_len >> 3);
	adts[5] = (uint8_t)(((frame_len & 0x07) << 5) | 0x1F);
	adts[6] = 0xFC;
	at += 7;
	memcpy(pes + at, data, size);
	at += size;

	return write_pes(mux, &mux->audio, pes, at, false, 0);
}

bool dsr_ts_mux_flush(struct dsr_ts_mux *mux)
{
	if (mux->failed)
		return false;
	if (mux->chunk_len == 0)
		return true;

	uint8_t null_packet[DSR_TS_PACKET_SIZE];
	memset(null_packet, 0xFF, sizeof(null_packet));
	null_packet[0] = 0x47;
	null_packet[1] = (uint8_t)(DSR_TS_PID_NULL >> 8);
	null_packet[2] = (uint8_t)DSR_TS_PID_NULL;
	null_packet[3] = 0x10;

	while (mux->chunk_len != 0) {
		if (!dsr_ts_emit_packet(mux, null_packet))
			return false;
	}
	return true;
}
