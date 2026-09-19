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

/* What the dock says beside the state: preflight notes, blockers, and the
 * mobile and dual format sides of the next stream. */

#include "relay-dock.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include "../ladder.h"
#include "../relay-limits.h"
#include "../relay-output.h"
#include "../relay-profile.h"
#include "../vertical-canvas.hpp"
#include "dsr-ui-common.hpp"
#include "vertical-common.hpp"
#include "vertical-dock.hpp"

/* The one preflight fact worth a banner: the video settings ask for something
 * the relay will not deliver. Everything the old card said about which
 * destinations are enabled is in the rows below it, and the delivered bitrate
 * is not something the user can act on, so neither is repeated here. Empty
 * when the settings and the relay agree, which is the common case. */
QString RelayDock::outputMismatch() const
{
	QStringList issues;
	const struct dsr_canvas_limits &limits = dsr_limits_get()->landscape;

	struct dsr_video_summary video;
	if (dsr_get_video_summary(&video)) {
		const uint32_t shortSide = video.output_width < video.output_height ? video.output_width
										    : video.output_height;
		if (shortSide > limits.max_height)
			issues.append(QString(dsrText("Preflight.Downscale")).arg(limits.max_height));
		if (video.fps > limits.max_fps + 0.5)
			issues.append(QString(dsrText("Preflight.FpsCap")).arg(qRound(limits.max_fps)));
	}

	/* A third above the tuned figure is where upload starts buying
	 * nothing; the relay refuses higher still, and that case is a blocker
	 * rather than a note. */
	const int high = limits.video_kbps * 4 / 3;
	if (dsr_get_configured_bitrate_kbps() > high)
		issues.append(QString(dsrText("Preflight.BitrateHigh")).arg(high));

	return issues.join(QStringLiteral(" "));
}

bool RelayDock::mobileNote(QString &text, QString &actionText, std::function<void()> &action)
{
	VerticalCanvas *vertical = VerticalCanvas::instance();
	if (!vertical)
		return false;

	bool wantsMobile = false;
	for (const DsrDestination &dest : destinations->list()) {
		if (dest.enabled && (dest.canvas == QLatin1String("portrait") || dest.canvas == QLatin1String("both")))
			wantsMobile = true;
	}

	if (!vertical->enabled()) {
		/* Off on purpose is a choice that stands. Off because the
		 * vertical dock was never opened is a gap, and a destination
		 * waiting on the canvas makes it one that costs a platform. */
		if (!wantsMobile && dsrReadFlag(kVerticalOffFlag))
			return false;
		text = dsrText(wantsMobile ? "Mobile.OffWanted" : "Mobile.Off");
		actionText = dsrText("Action.SetUpMobile");
		action = [this]() {
			VerticalCanvas *manager = VerticalCanvas::instance();
			if (manager && manager->ready() && dsrSetVerticalEnabled(this, true))
				dsrShowVerticalDock();
			refreshUi();
		};
		return true;
	}

	if (!wantsMobile) {
		text = dsrText("Mobile.NoDestination");
		return true;
	}
	return false;
}

/* Dual format needs three things the dock can see: an enabled Twitch
 * destination on both canvases, the portrait canvas on, and a build that
 * carries the multi-rendition output. The relay service asks for the answer
 * when OBS sets the next stream up. */
bool RelayDock::ladderWanted() const
{
	/* The relay's own service type is what asks for the output, so a
	 * profile still on the generic custom service cannot want a ladder. */
	if (!dsr_ladder_available() || !auth->signedIn() || !targetFetched || !dsr_route_is_relay() ||
	    dsr_route_needs_upgrade())
		return false;
	VerticalCanvas *vertical = VerticalCanvas::instance();
	if (!vertical || !vertical->enabled())
		return false;
	for (const DsrDestination &dest : destinations->list()) {
		if (dest.enabled && dest.platform == QLatin1String("twitch") && dest.canvas == QLatin1String("both"))
			return true;
	}
	return false;
}

QString RelayDock::ladderNote() const
{
	const size_t renditions = dsr_ladder_active_renditions();
	if (renditions == 0)
		return QString();
	return QString(dsrText("Ladder.Active")).arg((int)renditions);
}

/* OBS built this profile's outputs with a platform's Enhanced Broadcasting
 * switch still on. Every Start Streaming press fails with OBS's own dialog
 * until they are rebuilt without it, which a restart guarantees; OBS also
 * rebuilds them on its own when output settings are saved, which nothing
 * here can see, so this stays a note rather than a blocker. */
QString RelayDock::restartNote() const
{
	return dsr_profile_restart_pending() ? dsrText("Warning.RestartForRoute") : QString();
}

void RelayDock::pushLadderAuth()
{
	const QByteArray base = auth->apiBase().toUtf8();
	const QByteArray bearer = auth->bearerHeader();
	dsr_ladder_set_auth(base.constData(), bearer.constData());
}

QString RelayDock::blockingSetupIssue() const
{
	if (!dsr_route_is_relay())
		return QString();

	/* The relay takes H.264 video with AAC audio and nothing else. OBS
	 * will refuse to start on AV1 by itself, but it will happily send
	 * HEVC or Opus over SRT, the relay's pipeline decodes neither, and
	 * every platform then sits on the standby screen while OBS reports a
	 * healthy stream. Better said before the stream starts than
	 * discovered from the platforms. */
	char *codec = dsr_get_stream_video_codec();
	if (codec) {
		const QString name = QString::fromUtf8(codec);
		bfree(codec);
		if (name != QLatin1String("h264"))
			return QString(dsrText("Warning.VideoCodec")).arg(name.toUpper());
	}

	char *audio = dsr_get_stream_audio_codec();
	if (audio) {
		const QString name = QString::fromUtf8(audio);
		bfree(audio);
		if (name != QLatin1String("aac"))
			return QString(dsrText("Warning.AudioCodec")).arg(name.toUpper());
	}

	/* Twitch dictates the dual format ladder and refuses a fractional
	 * frame rate outright, at the moment Start Streaming is pressed. */
	if (ladderWanted()) {
		struct obs_video_info video;
		if (obs_get_video_info(&video) && video.fps_den > 0 && video.fps_num % video.fps_den != 0) {
			const double fps = (double)video.fps_num / (double)video.fps_den;
			return QString(dsrText("Warning.FractionalFps")).arg(QString::number(fps, 'f', 2));
		}
	}

	/* The relay's own gates. A keyframe more than a few seconds apart or a
	 * bitrate over the refusal line drops the stream to the relay's
	 * re-encode for the whole session. Where the relay service gets to set
	 * a figure at stream start nothing needs saying; where the profile
	 * runs its own, that figure is what gets checked. */
	const struct dsr_canvas_limits &limits = dsr_limits_get()->landscape;
	struct dsr_encoder_settings encoder;
	if (!dsr_encoder_read(&encoder))
		return QString();

	if (!dsr_route_applies_bitrate_cap() && encoder.video_bitrate_kbps > limits.unfit_kbps)
		return QString(dsrText("Warning.BitrateTooHigh")).arg(encoder.video_bitrate_kbps).arg(limits.unfit_kbps);

	if (!dsr_route_applies_keyint() && encoder.advanced &&
	    (encoder.keyint_sec <= 0 || encoder.keyint_sec > limits.keyint_max_sec)) {
		const QString interval = encoder.keyint_sec <= 0 ? dsrText("Warning.KeyintAuto")
								 : QString::number(encoder.keyint_sec);
		return QString(dsrText("Warning.KeyintTooLong")).arg(interval).arg(limits.keyint_sec);
	}

	return QString();
}

/* A connected account overwrites the relay key at every stream start. The
 * repair at the streaming-starting event undoes that, so it is a note rather
 * than a blocker, but it stays worth a word: the account also points other
 * OBS behavior (bandwidth tests, stream pages) at itself, and disconnecting
 * it removes the tug of war entirely. */
QString RelayDock::connectedAccountNote() const
{
	if (!dsr_route_is_relay())
		return QString();

	char *account = dsr_get_connected_account();
	if (!account)
		return QString();
	const QString name = QString::fromUtf8(account);
	bfree(account);
	return QString(dsrText("Warning.ConnectedAccount")).arg(name);
}

/* Why the last stream ended, when it ended on an error. The stop code and
 * the transport's own message are captured on the output's stop signal; the
 * code maps to a plain sentence and the message rides along verbatim, since
 * it is the most specific fact available. */
QString RelayDock::streamStopNotice() const
{
	/* The watch follows whatever output OBS started, which is not always
	 * one this plugin pointed anywhere. Reporting a stream that went
	 * straight to a platform would name the relay for a refusal it had no
	 * part in. */
	if (!dsr_route_is_relay())
		return QString();

	int code = 0;
	char *errorRaw = NULL;
	if (!dsr_stream_last_stop(&code, &errorRaw))
		return QString();

	const QString detail = QString::fromUtf8(errorRaw ? errorRaw : "");
	bfree(errorRaw);

	const char *reasonKey;
	switch (code) {
	case OBS_OUTPUT_CONNECT_FAILED:
	case OBS_OUTPUT_BAD_PATH:
		reasonKey = "StreamStop.ConnectFailed";
		break;
	case OBS_OUTPUT_DISCONNECTED:
		reasonKey = "StreamStop.Disconnected";
		break;
	case OBS_OUTPUT_INVALID_STREAM:
	case OBS_OUTPUT_UNSUPPORTED:
		reasonKey = "StreamStop.InvalidStream";
		break;
	case OBS_OUTPUT_ENCODE_ERROR:
		reasonKey = "StreamStop.EncodeError";
		break;
	default:
		reasonKey = "StreamStop.Error";
		break;
	}

	return QString(dsrText("Warning.StreamStopped")).arg(dsrText(reasonKey)).arg(detail).trimmed();
}
