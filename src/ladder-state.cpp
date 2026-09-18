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
 * credentials it hands over, and the prepare call that turns two canvases
 * into a Twitch ladder. Everything here may be read from the output's own
 * thread, so the shared state sits behind one lock and never touches Qt
 * objects that belong to the UI thread. */

#include "ladder.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstring>
#include <mutex>

#include <obs-module.h>
#include <plugin-support.h>

#include "relay-http.hpp"
#include "vertical-canvas.hpp"

namespace {

std::mutex stateMutex;
bool wantedFlag = false;
QByteArray apiBase;
QByteArray bearer;
size_t activeRenditions = 0;
bool preparedPending = false;
uint64_t sessionCounter = 0;

struct CanvasSpec {
	uint32_t width;
	uint32_t height;
	uint32_t fpsNum;
	uint32_t fpsDen;
};

bool mainCanvas(CanvasSpec &out)
{
	struct obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return false;
	out = {ovi.output_width, ovi.output_height, ovi.fps_num, ovi.fps_den};
	return true;
}

bool portraitCanvas(CanvasSpec &out)
{
	VerticalCanvas *vertical = VerticalCanvas::instance();
	if (!vertical)
		return false;
	obs_canvas_t *canvas = vertical->canvasRef();
	if (!canvas)
		return false;

	struct obs_video_info ovi;
	const bool ok = obs_canvas_get_video_info(canvas, &ovi);
	obs_canvas_release(canvas);
	if (!ok)
		return false;
	out = {ovi.output_width, ovi.output_height, ovi.fps_num, ovi.fps_den};
	return true;
}

QJsonObject canvasJson(const CanvasSpec &canvas)
{
	QJsonObject object;
	object.insert(QStringLiteral("width"), (int)canvas.width);
	object.insert(QStringLiteral("height"), (int)canvas.height);
	object.insert(QStringLiteral("fpsNum"), (int)canvas.fpsNum);
	object.insert(QStringLiteral("fpsDen"), (int)canvas.fpsDen);
	return object;
}

void setError(char *error, size_t errorLen, const QString &text)
{
	if (!error || errorLen == 0)
		return;
	const QByteArray utf8 = text.toUtf8();
	strncpy(error, utf8.constData(), errorLen - 1);
	error[errorLen - 1] = '\0';
}

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* One rendition of the response, refused when anything the encoder needs is
 * missing. Twitch decides the codec per rung and the plugin only offered
 * H.264, so any other answer is a contract change rather than a choice. */
bool readRendition(const QJsonObject &row, bool havePortrait, struct dsr_ladder_rendition &out)
{
	const QString codec = row.value(QStringLiteral("codec")).toString();
	if (!codec.isEmpty() && codec != QLatin1String("h264"))
		return false;

	out.canvas_index = row.value(QStringLiteral("canvasIndex")).toInt(0);
	out.width = (uint32_t)row.value(QStringLiteral("width")).toInt();
	out.height = (uint32_t)row.value(QStringLiteral("height")).toInt();
	out.fps_num = (uint32_t)row.value(QStringLiteral("fpsNum")).toInt();
	out.fps_den = (uint32_t)row.value(QStringLiteral("fpsDen")).toInt(1);
	out.bitrate_kbps = row.value(QStringLiteral("bitrateKbps")).toInt();
	out.keyint_sec = row.value(QStringLiteral("keyintSec")).toInt(2);

	if (out.canvas_index < 0 || out.canvas_index > 1 || (out.canvas_index == 1 && !havePortrait))
		return false;
	if (out.width == 0 || out.height == 0 || out.fps_num == 0 || out.bitrate_kbps <= 0)
		return false;
	if (out.fps_den == 0)
		out.fps_den = 1;
	if (out.keyint_sec <= 0)
		out.keyint_sec = 2;
	return true;
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

uint64_t dsr_ladder_begin_session(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
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

bool dsr_ladder_prepare(struct dsr_ladder_spec *out, const volatile bool *abort_flag, char *error, size_t error_len)
{
	memset(out, 0, sizeof(*out));

	CanvasSpec main;
	if (!mainCanvas(main)) {
		setError(error, error_len, text("Ladder.NoVideo"));
		return false;
	}

	QJsonArray canvases;
	canvases.append(canvasJson(main));
	CanvasSpec portrait;
	const bool havePortrait = portraitCanvas(portrait);
	if (havePortrait)
		canvases.append(canvasJson(portrait));

	/* H.264 only: the relay's slates are H.264, and a rung in any other
	 * codec would change codec at every splice. */
	QJsonObject body;
	body.insert(QStringLiteral("canvases"), canvases);
	body.insert(QStringLiteral("vodTrackAudio"), false);
	body.insert(QStringLiteral("testMode"), false);
	body.insert(QStringLiteral("hevc"), false);

	QByteArray base;
	QByteArray auth;
	{
		const std::lock_guard<std::mutex> lock(stateMutex);
		base = apiBase;
		auth = bearer;
	}
	if (base.isEmpty() || auth.isEmpty()) {
		setError(error, error_len, text("Ladder.NotSignedIn"));
		return false;
	}

	const DsrHttpReply reply = dsrHttpRequestSync("POST", base + "/api/relay/dual-format/prepare",
						      QJsonDocument(body).toJson(QJsonDocument::Compact), true, auth,
						      [abort_flag]() { return abort_flag && *abort_flag; });
	if (!reply.transportOk) {
		setError(error, error_len, text("Ladder.Unreachable"));
		return false;
	}

	const QJsonObject json = QJsonDocument::fromJson(reply.body).object();
	if (reply.status < 200 || reply.status >= 300) {
		QString code = json.value(QStringLiteral("code")).toString();
		if (code.isEmpty())
			code = QString::number(reply.status);
		obs_log(LOG_WARNING, "dual format prepare refused: %s", code.toUtf8().constData());
		setError(error, error_len, text("Ladder.Refused").arg(code));
		return false;
	}

	const QJsonObject ladder = json.value(QStringLiteral("ladder")).toObject();
	const QByteArray configId = ladder.value(QStringLiteral("configId")).toString().toUtf8();
	const QJsonArray rows = ladder.value(QStringLiteral("renditions")).toArray();
	if (configId.isEmpty() || rows.isEmpty() || rows.size() > DSR_LADDER_MAX_RENDITIONS) {
		setError(error, error_len, text("Ladder.BadLadder"));
		return false;
	}

	for (const QJsonValue &value : rows) {
		if (!readRendition(value.toObject(), havePortrait, out->renditions[out->count])) {
			setError(error, error_len, text("Ladder.BadLadder"));
			memset(out, 0, sizeof(*out));
			return false;
		}
		out->count++;
	}
	strncpy(out->config_id, configId.constData(), sizeof(out->config_id) - 1);

	{
		const std::lock_guard<std::mutex> lock(stateMutex);
		preparedPending = true;
	}
	obs_log(LOG_INFO, "dual format ladder prepared: %zu renditions across %d canvas(es)", out->count,
		havePortrait ? 2 : 1);
	return true;
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

void dsr_ladder_note_prepared(bool prepared)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	preparedPending = prepared;
}

bool dsr_ladder_prepared_pending(void)
{
	const std::lock_guard<std::mutex> lock(stateMutex);
	return preparedPending;
}
