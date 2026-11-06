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

/* The contribution plan: the renditions the relay wants this stream to carry
 * so each destination can be served the largest picture it accepts. A canvas
 * the relay cannot pass to every platform as one picture, a 4K one with a
 * Twitch destination on it, becomes the canvas itself plus a rung below it,
 * and the relay hands each destination the rendition that fits.
 *
 * Separate from the Twitch ladder next door: that one is Twitch's own layout
 * for one broadcast, this one is the relay's layout across destinations. A
 * session carries one or the other. Runs on the output's start thread. */

#include "ladder.h"
#include "ladder-internal.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstring>

#include <obs.h>
#include <obs-module.h>
#include <plugin-support.h>

#include "relay-http.hpp"

namespace {

const char *kPlanPath = "/api/relay/program/prepare";

/* The canvas the plan is asked for. Only OBS's own program: the portrait
 * canvas publishes separately, and its renditions would have to travel on
 * that publish rather than this one. */
bool programCanvas(QJsonObject &out, uint32_t &height)
{
	obs_video_info info;
	if (!obs_get_video_info(&info) || info.output_width == 0 || info.output_height == 0 || info.fps_num == 0)
		return false;
	out.insert(QStringLiteral("canvas"), QStringLiteral("landscape"));
	out.insert(QStringLiteral("width"), (int)info.output_width);
	out.insert(QStringLiteral("height"), (int)info.output_height);
	out.insert(QStringLiteral("fpsNum"), (int)info.fps_num);
	out.insert(QStringLiteral("fpsDen"), (int)(info.fps_den ? info.fps_den : 1));
	height = info.output_height < info.output_width ? info.output_height : info.output_width;
	return true;
}

/* One rendition of a plan. The shared rendition carries the canvas itself
 * and states a ceiling rather than a rate; every other rung is CBR at the
 * figure the plan set for it. */
bool readPlanRendition(const QJsonObject &row, struct dsr_ladder_rendition &out)
{
	const QString codec = row.value(QStringLiteral("codec")).toString();
	const bool hevc = codec == QLatin1String("h265");
	if (!codec.isEmpty() && !hevc && codec != QLatin1String("h264"))
		return false;

	const QJsonValue ceiling = row.value(QStringLiteral("ceilingKbps"));
	const QJsonValue bitrate = row.value(QStringLiteral("bitrateKbps"));

	out.canvas_index = 0;
	out.width = (uint32_t)row.value(QStringLiteral("width")).toInt();
	out.height = (uint32_t)row.value(QStringLiteral("height")).toInt();
	out.fps_num = (uint32_t)row.value(QStringLiteral("fpsNum")).toInt();
	out.fps_den = (uint32_t)row.value(QStringLiteral("fpsDen")).toInt(1);
	out.bitrate_kbps = bitrate.isDouble() ? bitrate.toInt() : ceiling.toInt();
	out.keyint_sec = row.value(QStringLiteral("keyintSec")).toInt(2);
	out.hevc = hevc;
	out.closed_gop = row.value(QStringLiteral("closedGop")).toBool();

	if (out.width == 0 || out.height == 0 || out.fps_num == 0 || out.bitrate_kbps <= 0)
		return false;
	if (out.fps_den == 0)
		out.fps_den = 1;
	if (out.keyint_sec <= 0)
		out.keyint_sec = 2;
	return true;
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

enum dsr_ladder_outcome dsrLadderAskPlan(struct dsr_ladder_spec *out, const volatile bool *abort_flag)
{
	memset(out, 0, sizeof(*out));

	QJsonObject canvas;
	uint32_t shortSide = 0;
	if (!programCanvas(canvas, shortSide))
		return DSR_LADDER_NONE;

	QJsonArray canvases;
	canvases.append(canvas);
	QJsonArray buses;
	buses.append(0);

	QJsonObject body;
	body.insert(QStringLiteral("canvases"), canvases);
	/* One bus: the plugin sends OBS's program audio and nothing beside it. */
	body.insert(QStringLiteral("audioBuses"), buses);
	/* The encoder session budget is the desktop app's measurement; without
	 * one the website plans against no limit, and a rung this machine
	 * cannot open fails at encoder creation with its own message. */
	body.insert(QStringLiteral("encoderSessionsAvailable"), QJsonValue());
	body.insert(QStringLiteral("ebConfigId"), QJsonValue());
	body.insert(QStringLiteral("mode"), QStringLiteral("replace"));

	QByteArray base;
	QByteArray bearer;
	dsrLadderAuth(base, bearer);
	if (base.isEmpty() || bearer.isEmpty())
		return DSR_LADDER_NONE;

	const DsrHttpReply reply = dsrHttpRequestSync("POST", base + kPlanPath,
						      QJsonDocument(body).toJson(QJsonDocument::Compact), true, bearer,
						      [abort_flag]() { return abort_flag && *abort_flag; });
	if (abort_flag && *abort_flag)
		return DSR_LADDER_ABORTED;

	if (!reply.transportOk) {
		obs_log(LOG_WARNING, "contribution plan could not reach %s: %s", base.constData(),
			reply.transportError.constData());
		return DSR_LADDER_NONE;
	}

	const QJsonObject json = QJsonDocument::fromJson(reply.body).object();
	if (reply.status < 200 || reply.status >= 300) {
		const QString code = json.value(QStringLiteral("code")).toString();
		/* Refusals here are all "not for this account, not today": the
		 * stream goes out as one picture, which is what it did before
		 * plans existed. Nothing is said to the streamer about it. */
		obs_log(LOG_INFO, "contribution plan not granted (%s); publishing a single picture",
			code.isEmpty() ? QByteArray::number(reply.status).constData() : code.toUtf8().constData());
		return DSR_LADDER_NONE;
	}

	const QJsonObject landscape = json.value(QStringLiteral("plan"))
					      .toObject()
					      .value(QStringLiteral("canvases"))
					      .toObject()
					      .value(QStringLiteral("landscape"))
					      .toObject();
	const QJsonArray rows = landscape.value(QStringLiteral("renditions")).toArray();
	if (rows.isEmpty() || rows.size() > DSR_LADDER_MAX_RENDITIONS) {
		dsr_ladder_discard(abort_flag);
		return DSR_LADDER_NONE;
	}

	/* Index order is the contract: rendition i travels on video PID
	 * 0x200 + i and the relay binds the destinations by that order, so a
	 * rung that cannot be read here makes the whole plan unusable rather
	 * than shifting the ones behind it. */
	for (const QJsonValue &value : rows) {
		if (!readPlanRendition(value.toObject(), out->renditions[out->count])) {
			memset(out, 0, sizeof(*out));
			dsr_ladder_discard(abort_flag);
			return DSR_LADDER_NONE;
		}
		out->count++;
	}
	/* A plan of one rendition is the canvas on its own, which is what a
	 * plain stream already sends. */
	if (out->count < 2) {
		memset(out, 0, sizeof(*out));
		dsr_ladder_discard(abort_flag);
		return DSR_LADDER_NONE;
	}

	dsrLadderSetPreparedPending(true);
	obs_log(LOG_INFO, "contribution plan prepared: %zu renditions on the desktop canvas", out->count);
	return DSR_LADDER_READY;
}
