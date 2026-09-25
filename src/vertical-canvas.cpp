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

#include "vertical-canvas.hpp"

#include <QMetaObject>
#include <QSet>
#include <QVector>

#include <graphics/vec2.h>
#include <obs-module.h>
#include <plugin-support.h>

#include "ladder.h"
#include "relay-output.h"
#include "vertical-geometry.hpp"

namespace {

/* Identifier, not display text: the frontend saves the canvas into the scene
 * collection under this name and adoption after a restart looks it up by the
 * same string, so it must never vary by locale or version. */
const char *kCanvasName = "DualStream Vertical";

/* Sources on the canvas are activated so they capture, and its scenes are
 * kept by it. It does not mix its audio: OBS would sum every source shown on
 * both canvases twice into every track, and the vertical stream takes its
 * audio from a track of its own instead. */
const uint32_t kCanvasFlags = ACTIVATE | SCENE_REF;

/* The name a superseded canvas carries while its scenes move out. */
const char *kRetiredCanvasName = "DualStream Vertical (retired)";

VerticalCanvas *singleton = nullptr;

bool collectSceneRefs(void *param, obs_source_t *source)
{
	static_cast<QVector<obs_source_t *> *>(param)->append(obs_source_get_ref(source));
	return true;
}

void portraitVideoInfo(struct obs_video_info *ovi)
{
	/* Frame rate, format and color space follow the profile; only the
	 * geometry is ours. Composited at delivery size, so no scaling pass
	 * sits between the canvas and the encoder. */
	obs_get_video_info(ovi);
	ovi->base_width = dsrPortraitWidth();
	ovi->base_height = dsrPortraitHeight();
	ovi->output_width = dsrPortraitWidth();
	ovi->output_height = dsrPortraitHeight();
}

} // namespace

VerticalCanvas *VerticalCanvas::instance()
{
	return singleton;
}

VerticalCanvas::VerticalCanvas(QObject *parent) : QObject(parent)
{
	singleton = this;
	/* Connected for the lifetime of the plugin so the transition mirror
	 * follows every swap of OBS's program channel, including the temporary
	 * one a per-scene override does. */
	signal_handler_connect(obs_get_signal_handler(), "channel_change", onChannelChange, this);
}

VerticalCanvas::~VerticalCanvas()
{
	signal_handler_disconnect(obs_get_signal_handler(), "channel_change", onChannelChange, this);
	teardown();
	singleton = nullptr;
}

/* An output exists from the moment it is asked to start until its stop is
 * seen, and it counts as publishing for that whole span. OBS reports it
 * active only once it has connected, and the plugin used to ask for a
 * second start in that window: the start path releases the previous
 * output first, and releasing one that is still connecting, encoders and
 * all, brought OBS down (two crashes on 2026-09-21 as the ingest target
 * fetch landed 150 ms into a mobile start). */
bool VerticalCanvas::publishing() const
{
	return output != nullptr;
}

obs_canvas_t *VerticalCanvas::canvasRef() const
{
	const QMutexLocker lock(&canvasMutex);
	return canvas ? obs_canvas_get_ref(canvas) : nullptr;
}

void VerticalCanvas::setCanvasHandle(obs_canvas_t *next)
{
	const QMutexLocker lock(&canvasMutex);
	canvas = next;
}

/* Whether anything is encoding from this canvas's mix, or may be about to:
 * the mobile output, or the stream output from the moment it takes the
 * service, since dual format builds its encoders on this mix during that
 * window. Other outputs never read this mix. */
bool VerticalCanvas::canvasInUse() const
{
	return publishing() || obs_frontend_streaming_active() || dsr_stream_output_engaged();
}

/* A dual format session leaves its encoders on the stream output between
 * streams, and they point at this canvas's video mix. They go before the
 * mix does. */
void VerticalCanvas::forgetLadderEncoders()
{
	obs_output_t *stream = obs_frontend_get_streaming_output();
	if (!stream)
		return;
	dsr_ladder_output_forget_session(stream);
	obs_output_release(stream);
}

obs_source_t *VerticalCanvas::counterpartOf(obs_source_t *landscapeScene) const
{
	if (!canvas || !landscapeScene)
		return nullptr;

	const char *name = obs_source_get_name(landscapeScene);
	if (!name)
		return nullptr;

	obs_scene_t *counterpart = obs_canvas_get_scene_by_name(canvas, name);
	return counterpart ? obs_scene_get_source(counterpart) : nullptr;
}

obs_source_t *VerticalCanvas::currentCounterpart() const
{
	obs_source_t *current = obs_frontend_get_current_scene();
	obs_source_t *counterpart = counterpartOf(current);
	obs_source_release(current);
	return counterpart;
}

obs_source_t *VerticalCanvas::editingLandscapeScene() const
{
	if (obs_frontend_preview_program_mode_active()) {
		obs_source_t *preview = obs_frontend_get_current_preview_scene();
		if (preview)
			return preview;
	}
	return obs_frontend_get_current_scene();
}

obs_source_t *VerticalCanvas::editingCounterpart() const
{
	obs_source_t *editing = editingLandscapeScene();
	obs_source_t *counterpart = counterpartOf(editing);
	obs_source_release(editing);
	return counterpart;
}

obs_source_t *VerticalCanvas::studioPreviewCounterpart() const
{
	if (!obs_frontend_preview_program_mode_active())
		return nullptr;
	obs_source_t *editing = editingCounterpart();
	obs_source_t *program = currentCounterpart();
	const bool same = editing == program;
	obs_source_release(program);
	if (same) {
		obs_source_release(editing);
		return nullptr;
	}
	return editing;
}

void VerticalCanvas::setEnabled(bool on)
{
	if (on == enabled())
		return;

	if (on) {
		struct obs_video_info ovi;
		portraitVideoInfo(&ovi);
		obs_canvas_t *fresh = obs_frontend_add_canvas(kCanvasName, &ovi, kCanvasFlags);
		if (!fresh) {
			obs_log(LOG_ERROR, "vertical canvas could not be created");
			return;
		}
		setCanvasHandle(fresh);
		reconcileScenes();
		hookCurrentTransition();
		showCurrentScene();
		obs_log(LOG_INFO, "vertical canvas enabled");
	} else {
		setSelectedItemId(-1);
		stopOutput(true);
		releaseOutput();
		releaseTransition();
		disconnectAllSceneSignals();
		forgetLadderEncoders();
		/* Let go of the handle before the canvas goes, so nothing
		 * reading it from another thread can take a reference to a
		 * canvas that is being destroyed. */
		obs_canvas_t *going = canvas;
		setCanvasHandle(nullptr);
		obs_frontend_remove_canvas(going);
		obs_canvas_release(going);
		obs_log(LOG_INFO, "vertical canvas disabled");
	}

	obs_frontend_save();
	emit changed();
}

/* Only the relay dock knows whether the subscription is active, so it says so
 * here rather than this class reaching for it. */
void VerticalCanvas::setDirectAllowed(bool allowed)
{
	if (directAllowedFlag == allowed)
		return;
	directAllowedFlag = allowed;
	emit changed();
}

void VerticalCanvas::notifyDirectChanged()
{
	emit changed();
}

void VerticalCanvas::setPortraitTarget(const QString &server, const QString &key)
{
	portraitServer = server;
	portraitKey = key;
	maybeStartOutput();
}

/* Always emits, even for the same id: a rebuild may have dropped a widget's
 * local notion of the selection, and re-announcing is how it resyncs. */
void VerticalCanvas::setSelectedItemId(int64_t id)
{
	selectedItem = id;
	mirrorSelection(id);
	emit selectionChanged(id);
}

void VerticalCanvas::setHasPortraitDestinations(bool has)
{
	if (hasPortraitDests == has)
		return;
	hasPortraitDests = has;
	maybeStartOutput();
	emit changed();
}

void VerticalCanvas::setHasDualFormatDestination(bool has)
{
	if (hasDualFormatDest == has)
		return;
	hasDualFormatDest = has;
	emit changed();
}

/* Pick up a canvas the scene collection already carries. The frontend
 * recreates it at collection load, but deliberately without a video mix (the
 * saved form keeps only name, uuid and flags), so giving it one at our
 * dimensions is this plugin's job. libobs refuses to add a mix to an
 * existing canvas while any output runs, whatever that output is on, and a
 * streamer whose OBS starts an output on launch (a virtual camera, an NDI
 * feed, a recording) would otherwise get a canvas that renders nothing. A
 * canvas can always be created with its mix, so the scenes move to a fresh
 * one instead. The same road serves a canvas saved by an earlier release as
 * a program canvas, which mixes its audio into every track. */
void VerticalCanvas::adopt()
{
	teardown();

	obs_canvas_t *found = obs_get_canvas_by_name(kCanvasName);
	/* A replacement that was cut short leaves the canvas under its
	 * retiring name; it is picked up and the replacement finished. */
	if (!found)
		found = obs_get_canvas_by_name(kRetiredCanvasName);
	if (found) {
		setCanvasHandle(found);
		const bool mixesAudio = (obs_canvas_get_flags(found) & MIX_AUDIO) != 0;
		bool withoutVideo = false;
		if (!mixesAudio && !obs_canvas_has_video(found)) {
			struct obs_video_info ovi;
			portraitVideoInfo(&ovi);
			withoutVideo = !obs_canvas_reset_video(found, &ovi);
		}
		if (mixesAudio)
			replaceCanvas("it was saved as a program canvas and mixes its audio into every track", true);
		else if (withoutVideo)
			replaceCanvas("an output is running and a video mix cannot be added to it in place", false);
		reconcileScenes();
		hookCurrentTransition();
		showCurrentScene();
	}

	/* Emitted with or without a canvas: the dock's default-on behavior
	 * hangs off this signal after the collection loads. */
	emit changed();
}

/* Move every scene to a fresh canvas and let the old one go. Deferred, when
 * asked, while the old canvas's mix is feeding a stream; a canvas with no
 * mix feeds nothing and is replaced at once. */
void VerticalCanvas::replaceCanvas(const char *reason, bool deferWhileInUse)
{
	/* Only a canvas with a mix can be feeding anything; one loaded
	 * without a mix is replaced at once whatever else is running. */
	replacePending = deferWhileInUse && obs_canvas_has_video(canvas) && canvasInUse();
	if (replacePending) {
		obs_log(LOG_INFO, "vertical canvas kept until the stream ends: %s", reason);
		return;
	}

	struct obs_video_info ovi;
	portraitVideoInfo(&ovi);
	obs_canvas_t *old = canvas;
	obs_canvas_set_name(old, kRetiredCanvasName);
	obs_canvas_t *fresh = obs_frontend_add_canvas(kCanvasName, &ovi, kCanvasFlags);
	if (!fresh) {
		obs_canvas_set_name(old, kCanvasName);
		obs_log(LOG_WARNING, "vertical canvas could not be replaced (%s)", reason);
		return;
	}

	QVector<obs_source_t *> scenes;
	obs_canvas_enum_scenes(old, collectSceneRefs, &scenes);
	for (obs_source_t *scene : scenes) {
		obs_canvas_move_scene(obs_scene_from_source(scene), fresh);
		obs_source_release(scene);
	}

	forgetLadderEncoders();
	setCanvasHandle(fresh);
	/* Marked removed before it is let go of: a reference held elsewhere
	 * for a moment longer, the preview's for one, must not get it saved
	 * into the collection under its retiring name. */
	obs_canvas_remove(old);
	obs_frontend_remove_canvas(old);
	obs_canvas_release(old);
	obs_frontend_save();
	obs_log(LOG_INFO, "vertical canvas replaced, %d scene(s) moved: %s", (int)scenes.size(), reason);
}

/* Work that had to wait for the canvas to go idle, or for a source to report
 * its size, gets another go at the moments that change either. A canvas
 * replacement is a fresh adoption, so everything hooked to the old one is
 * let go of first. */
void VerticalCanvas::retryDeferredWork()
{
	if (!canvas)
		return;
	if (replacePending && !canvasInUse())
		adopt();
	else if (layoutsDeferred)
		reconcileScenes();
}

void VerticalCanvas::teardown()
{
	setSelectedItemId(-1);
	stopOutput(true);
	releaseOutput();
	releaseTransition();
	disconnectAllSceneSignals();
	if (canvas) {
		obs_canvas_t *going = canvas;
		setCanvasHandle(nullptr);
		obs_canvas_release(going);
	}
}

void VerticalCanvas::ensureVideo()
{
	if (!canvas || obs_canvas_has_video(canvas))
		return;

	struct obs_video_info ovi;
	portraitVideoInfo(&ovi);
	if (!obs_canvas_reset_video(canvas, &ovi))
		obs_log(LOG_WARNING, "vertical canvas video could not be restored; is an output active?");
}

void VerticalCanvas::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		frontendReady = true;
		adopt();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		/* The canvas belongs to the collection being closed. Let go of
		 * everything before the frontend destroys it. */
		teardown();
		emit changed();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		adopt();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		/* Raised once the transition has finished, by which point the
		 * portrait mix has run its own copy of it; this settles the
		 * result rather than moving it. The selection names an item of
		 * the outgoing scene. */
		setSelectedItemId(-1);
		showCurrentScene();
		emit changed();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		reconcileScenes();
		showCurrentScene();
		emit changed();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		maybeStartOutput();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		maybeStartOutput();
		retryDeferredWork();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
	case OBS_FRONTEND_EVENT_VIRTUALCAM_STOPPED:
	case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
		retryDeferredWork();
		break;
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		/* OBS sets its program transition rather than starting it on
		 * both switches, so nothing is mirrored; the canvas is settled
		 * on the program scene by hand. The docks switch between the
		 * program and the preview scene. */
		hookCurrentTransition();
		showCurrentScene();
		setSelectedItemId(-1);
		emit changed();
		break;
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		/* Studio mode: the docks edit the preview scene while the
		 * mobile program keeps the program scene until the transition. */
		setSelectedItemId(-1);
		emit changed();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		/* The vertical stream's audio track is chosen from the profile's
		 * output settings, so the docks read it again. */
		emit changed();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		stopOutput(false);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		teardown();
		break;
	default:
		break;
	}
}
