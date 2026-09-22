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

/* The prepare call that turns the canvases into a Twitch ladder, and what
 * happens when the answer cannot be encoded here. Twitch decides the codec
 * per rung, and for some channels it wants HEVC for the mobile picture
 * whatever the machine offered. A machine with an HEVC encoder simply
 * encodes it. One without is asked for again without the mobile canvas, and
 * failing that streams OBS's own encoder alone. Start Streaming never fails
 * on any of this; the dock is told what was left out. Runs on the output's
 * start thread. */

#include "ladder.h"
#include "ladder-internal.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <cstring>

#include <obs-module.h>
#include <plugin-support.h>

#include "encoder-choice.hpp"
#include "relay-http.hpp"
#include "vertical-canvas.hpp"

namespace {

const char *kPreparePath = "/api/relay/dual-format/prepare";

/* The most video tracks asked of Twitch. The website accepts eight at most
 * and the output carries ten, so eight bounds the ladder without ever being
 * refused. */
const int kMaxVideoTracks = 8;

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

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* How one answer from the website ended. */
enum class Asked { Ready, Unencodable, Failed };

struct Answer {
	Asked kind;
	/* Failed: what went wrong, for the streamer. Unencodable: the rung
	 * this machine has no encoder for, as "1080x1920 at 60 fps". */
	QString detail;
	/* The website kept a configuration for this answer, which has to be
	 * taken down if the session does not send it. */
	bool stored;
};

/* One rendition of the response, refused when anything the encoder needs is
 * missing. A codec this machine cannot produce is not a refusal but a rung
 * to be asked about differently. */
bool readRendition(const QJsonObject &row, bool havePortrait, bool hevcAvailable, struct dsr_ladder_rendition &out,
		   QString &unencodable)
{
	const QString codec = row.value(QStringLiteral("codec")).toString();
	const bool hevc = codec == QLatin1String("h265") || codec == QLatin1String("hevc");
	if (!codec.isEmpty() && !hevc && codec != QLatin1String("h264"))
		return false;

	out.canvas_index = row.value(QStringLiteral("canvasIndex")).toInt(0);
	out.width = (uint32_t)row.value(QStringLiteral("width")).toInt();
	out.height = (uint32_t)row.value(QStringLiteral("height")).toInt();
	out.fps_num = (uint32_t)row.value(QStringLiteral("fpsNum")).toInt();
	out.fps_den = (uint32_t)row.value(QStringLiteral("fpsDen")).toInt(1);
	out.bitrate_kbps = row.value(QStringLiteral("bitrateKbps")).toInt();
	out.keyint_sec = row.value(QStringLiteral("keyintSec")).toInt(2);
	out.hevc = hevc;

	if (out.canvas_index < 0 || out.canvas_index > 1 || (out.canvas_index == 1 && !havePortrait))
		return false;
	if (out.width == 0 || out.height == 0 || out.fps_num == 0 || out.bitrate_kbps <= 0)
		return false;
	if (out.fps_den == 0)
		out.fps_den = 1;
	if (out.keyint_sec <= 0)
		out.keyint_sec = 2;
	if (hevc && !hevcAvailable && unencodable.isEmpty()) {
		const double fps = (double)out.fps_num / (double)out.fps_den;
		unencodable = QStringLiteral("%1x%2 at %3 fps").arg(out.width).arg(out.height).arg(fps, 0, 'g', 4);
	}
	return true;
}

Answer ask(const QJsonArray &canvases, bool havePortrait, bool hevcAvailable, const volatile bool *abort_flag,
	   struct dsr_ladder_spec *out)
{
	memset(out, 0, sizeof(*out));

	QJsonObject body;
	body.insert(QStringLiteral("canvases"), canvases);
	body.insert(QStringLiteral("vodTrackAudio"), false);
	body.insert(QStringLiteral("testMode"), false);
	body.insert(QStringLiteral("hevc"), hevcAvailable);
	body.insert(QStringLiteral("maxVideoTracks"), kMaxVideoTracks);

	QByteArray base;
	QByteArray auth;
	dsrLadderAuth(base, auth);
	if (base.isEmpty() || auth.isEmpty())
		return {Asked::Failed, text("Ladder.NotSignedIn"), false};

	const DsrHttpReply reply = dsrHttpRequestSync("POST", base + kPreparePath,
						      QJsonDocument(body).toJson(QJsonDocument::Compact), true, auth,
						      [abort_flag]() { return abort_flag && *abort_flag; });
	if (!reply.transportOk) {
		obs_log(LOG_WARNING, "dual format prepare could not reach %s: %s", base.constData(),
			reply.transportError.constData());
		return {Asked::Failed, text("Ladder.Unreachable").arg(QString::fromUtf8(reply.transportError)), false};
	}

	const QJsonObject json = QJsonDocument::fromJson(reply.body).object();
	if (reply.status < 200 || reply.status >= 300) {
		QString code = json.value(QStringLiteral("code")).toString();
		if (code.isEmpty())
			code = QString::number(reply.status);
		const QString message = json.value(QStringLiteral("message")).toString().trimmed();
		obs_log(LOG_WARNING, "dual format prepare refused: %s (%s)", code.toUtf8().constData(),
			message.toUtf8().constData());
		/* Twitch's own refusal names what it objected to, in words meant
		 * for the streamer, and is the one that gets shown as is. */
		if (code == QLatin1String("TWITCH_DECLINED") && !message.isEmpty())
			return {Asked::Failed, text("Ladder.Declined").arg(message), false};
		return {Asked::Failed, text("Ladder.Refused").arg(code), false};
	}

	const QJsonObject ladder = json.value(QStringLiteral("ladder")).toObject();
	const QByteArray configId = ladder.value(QStringLiteral("configId")).toString().toUtf8();
	const QJsonArray rows = ladder.value(QStringLiteral("renditions")).toArray();
	if (configId.isEmpty() || rows.isEmpty() || rows.size() > DSR_LADDER_MAX_RENDITIONS)
		return {Asked::Failed, text("Ladder.BadLadder"), true};

	QString unencodable;
	for (const QJsonValue &value : rows) {
		if (!readRendition(value.toObject(), havePortrait, hevcAvailable, out->renditions[out->count],
				   unencodable)) {
			memset(out, 0, sizeof(*out));
			return {Asked::Failed, text("Ladder.BadLadder"), true};
		}
		out->count++;
	}
	strncpy(out->config_id, configId.constData(), sizeof(out->config_id) - 1);
	if (!unencodable.isEmpty()) {
		obs_log(LOG_INFO, "dual format ladder asks for %s in HEVC, which this machine cannot encode",
			unencodable.toUtf8().constData());
		return {Asked::Unencodable, unencodable, true};
	}
	return {Asked::Ready, QString(), true};
}

} // namespace

/* A prepared configuration this session will not send is taken down at
 * once, so the relay builds the session around what actually arrives. */
void dsr_ladder_discard(const volatile bool *abort_flag)
{
	QByteArray base;
	QByteArray auth;
	dsrLadderAuth(base, auth);
	if (base.isEmpty() || auth.isEmpty())
		return;
	const DsrHttpReply reply = dsrHttpRequestSync("DELETE", base + kPreparePath, QByteArray(), false, auth,
						      [abort_flag]() { return abort_flag && *abort_flag; });
	if (!reply.transportOk || reply.status < 200 || reply.status >= 300) {
		obs_log(LOG_WARNING, "dual format configuration could not be taken down (%d)", reply.status);
		return;
	}
	dsrLadderSetPreparedPending(false);
}

enum dsr_ladder_outcome dsr_ladder_prepare(struct dsr_ladder_spec *out, const volatile bool *abort_flag)
{
	memset(out, 0, sizeof(*out));

	CanvasSpec main;
	if (!mainCanvas(main)) {
		dsrLadderSetNote(text("Ladder.Fallback").arg(text("Ladder.NoVideo")));
		return DSR_LADDER_NONE;
	}
	CanvasSpec portrait;
	const bool havePortrait = portraitCanvas(portrait);
	const bool hevcAvailable = dsrPickHevcEncoderId() != nullptr;

	QJsonArray canvases;
	canvases.append(canvasJson(main));
	if (havePortrait)
		canvases.append(canvasJson(portrait));

	/* A configuration the website kept is pending from the moment it
	 * exists, whatever becomes of this start: the dock takes it down when
	 * the stream stops if nothing here does first. */
	Answer answer = ask(canvases, havePortrait, hevcAvailable, abort_flag, out);
	bool stored = answer.stored;
	if (stored)
		dsrLadderSetPreparedPending(true);
	if (abort_flag && *abort_flag)
		return DSR_LADDER_ABORTED;
	if (answer.kind == Asked::Ready) {
		obs_log(LOG_INFO, "dual format ladder prepared: %zu renditions across %d canvas(es)", out->count,
			havePortrait ? 2 : 1);
		return DSR_LADDER_READY;
	}

	/* The mobile picture is what draws an HEVC rung. Asked for the desktop
	 * canvas alone, Twitch answers in H.264 and the desktop viewers keep
	 * their ladder; the mobile picture is what this stream does without. */
	if (answer.kind == Asked::Unencodable && havePortrait) {
		const QString cannot = text("Ladder.CannotEncode").arg(answer.detail);
		QJsonArray desktopOnly;
		desktopOnly.append(canvasJson(main));
		const Answer again = ask(desktopOnly, false, hevcAvailable, abort_flag, out);
		stored = stored || again.stored;
		if (stored)
			dsrLadderSetPreparedPending(true);
		if (abort_flag && *abort_flag)
			return DSR_LADDER_ABORTED;
		if (again.kind == Asked::Ready) {
			dsrLadderSetNote(text("Ladder.FallbackMobile").arg(cannot));
			obs_log(LOG_INFO, "dual format ladder prepared for the desktop canvas only: %zu renditions",
				out->count);
			return DSR_LADDER_DESKTOP_ONLY;
		}
		if (again.kind == Asked::Unencodable)
			answer =
				Answer{Asked::Unencodable, again.detail.isEmpty() ? answer.detail : again.detail, true};
		else
			answer = again;
	}

	memset(out, 0, sizeof(*out));
	if (stored) {
		dsr_ladder_discard(abort_flag);
		if (abort_flag && *abort_flag)
			return DSR_LADDER_ABORTED;
	}
	if (answer.kind == Asked::Unencodable)
		dsrLadderSetNote(text("Ladder.Fallback").arg(text("Ladder.CannotEncode").arg(answer.detail)));
	else
		dsrLadderSetNote(text("Ladder.Fallback").arg(answer.detail));
	obs_log(LOG_WARNING, "dual format is off for this session: %s", answer.detail.toUtf8().constData());
	return DSR_LADDER_NONE;
}
