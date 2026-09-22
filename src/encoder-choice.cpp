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

#include "encoder-choice.hpp"

#include <cstring>

#include <obs.h>

namespace {

/* The first of the preferred encoder types this OBS has registered. */
const char *firstRegistered(const char *const *preferred, size_t count)
{
	for (size_t p = 0; p < count; p++) {
		const char *id = nullptr;
		for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
			if (id && strcmp(id, preferred[p]) == 0)
				return preferred[p];
		}
	}
	return nullptr;
}

} // namespace

const char *dsrPickH264EncoderId()
{
	static const char *const preferred[] = {"obs_nvenc_h264_tex", "ffmpeg_nvenc", "obs_qsv11_v2",
						"h264_texture_amf"};
	const char *id = firstRegistered(preferred, sizeof(preferred) / sizeof(preferred[0]));
	return id ? id : "obs_x264";
}

const char *dsrPickHevcEncoderId()
{
	static const char *const preferred[] = {"obs_nvenc_hevc_tex", "obs_nvenc_hevc_cuda", "ffmpeg_hevc_nvenc",
						"obs_qsv11_hevc", "h265_texture_amf"};
	return firstRegistered(preferred, sizeof(preferred) / sizeof(preferred[0]));
}
