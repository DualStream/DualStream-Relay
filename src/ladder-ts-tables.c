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

/* The program tables of the contribution: one program, its map naming every
 * video PID in ladder order and the audio PID, and the clock reference on
 * the primary video. */

#include "ladder-ts-internal.h"

#include <string.h>

#define PROGRAM_NUMBER 1
#define TRANSPORT_STREAM_ID 1
#define STREAM_TYPE_H264 0x1B
#define STREAM_TYPE_AAC 0x0F

/* CRC-32/MPEG-2, as the tables carry it. */
static uint32_t crc32_mpeg(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= (uint32_t)data[i] << 24;
		for (int bit = 0; bit < 8; bit++)
			crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
	}
	return crc;
}

static void put_crc(uint8_t *section, size_t len)
{
	const uint32_t crc = crc32_mpeg(section, len);
	section[len] = (uint8_t)(crc >> 24);
	section[len + 1] = (uint8_t)(crc >> 16);
	section[len + 2] = (uint8_t)(crc >> 8);
	section[len + 3] = (uint8_t)crc;
}

/* One table section in one packet. Every table here is far shorter than a
 * packet, so no section ever spans two. */
static bool write_section(struct dsr_ts_mux *mux, uint16_t pid, uint8_t *continuity, const uint8_t *section, size_t len)
{
	uint8_t packet[DSR_TS_PACKET_SIZE];
	packet[0] = 0x47;
	packet[1] = 0x40 | (uint8_t)(pid >> 8);
	packet[2] = (uint8_t)pid;
	packet[3] = 0x10 | (*continuity & 0x0F);
	(*continuity)++;
	packet[4] = 0x00;
	memcpy(packet + 5, section, len);
	memset(packet + 5 + len, 0xFF, DSR_TS_PACKET_SIZE - 5 - len);
	return dsr_ts_emit_packet(mux, packet);
}

bool dsr_ts_write_tables(struct dsr_ts_mux *mux)
{
	uint8_t pat[16];
	pat[0] = 0x00;
	pat[1] = 0xB0;
	pat[2] = 13;
	pat[3] = (uint8_t)(TRANSPORT_STREAM_ID >> 8);
	pat[4] = (uint8_t)TRANSPORT_STREAM_ID;
	pat[5] = 0xC1;
	pat[6] = 0x00;
	pat[7] = 0x00;
	pat[8] = (uint8_t)(PROGRAM_NUMBER >> 8);
	pat[9] = (uint8_t)PROGRAM_NUMBER;
	pat[10] = 0xE0 | (uint8_t)(DSR_TS_PID_PMT >> 8);
	pat[11] = (uint8_t)DSR_TS_PID_PMT;
	put_crc(pat, 12);
	if (!write_section(mux, DSR_TS_PID_PAT, &mux->pat_continuity, pat, sizeof(pat)))
		return false;

	/* The clock rides with the primary video. */
	const uint16_t pcr_pid = mux->video_count ? mux->video[0].pid : DSR_TS_PID_NULL;
	const size_t streams = mux->video_count + (mux->have_audio ? 1 : 0);
	const size_t length = 9 + 5 * streams + 4;
	uint8_t pmt[3 + 9 + 5 * (DSR_TS_MAX_VIDEO + 1) + 4];
	pmt[0] = 0x02;
	pmt[1] = 0xB0 | (uint8_t)(length >> 8);
	pmt[2] = (uint8_t)length;
	pmt[3] = (uint8_t)(PROGRAM_NUMBER >> 8);
	pmt[4] = (uint8_t)PROGRAM_NUMBER;
	pmt[5] = 0xC1;
	pmt[6] = 0x00;
	pmt[7] = 0x00;
	pmt[8] = 0xE0 | (uint8_t)(pcr_pid >> 8);
	pmt[9] = (uint8_t)pcr_pid;
	pmt[10] = 0xF0;
	pmt[11] = 0x00;
	size_t at = 12;
	for (size_t i = 0; i < mux->video_count; i++) {
		pmt[at++] = STREAM_TYPE_H264;
		pmt[at++] = 0xE0 | (uint8_t)(mux->video[i].pid >> 8);
		pmt[at++] = (uint8_t)mux->video[i].pid;
		pmt[at++] = 0xF0;
		pmt[at++] = 0x00;
	}
	if (mux->have_audio) {
		pmt[at++] = STREAM_TYPE_AAC;
		pmt[at++] = 0xE0 | (uint8_t)(mux->audio.pid >> 8);
		pmt[at++] = (uint8_t)mux->audio.pid;
		pmt[at++] = 0xF0;
		pmt[at++] = 0x00;
	}
	put_crc(pmt, at);
	return write_section(mux, DSR_TS_PID_PMT, &mux->pmt_continuity, pmt, at + 4);
}
