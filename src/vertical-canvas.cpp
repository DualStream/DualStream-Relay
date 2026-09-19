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

bool VerticalCanvas::publishing() const
{
	return output && obs_output_active(output);
}

obs_canvas_t *VerticalCanvas::canvasRef() const
{
	return canvas ? obs_canvas_get_ref(canvas) : nullptr;
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

void VerticalCanvas::setEnabled(bool on)
{
	if (on == enabled())
		return;

	if (on) {
		struct obs_video_info ovi;
		portraitVideoInfo(&ovi);
		canvas = obs_frontend_add_canvas(kCanvasName, &ovi, kCanvasFlags);
		if (!canvas) {
			obs_log(LOG_ERROR, "vertical canvas could not be created");
			return;
		}
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
		/* Let go of the handle before the canvas goes, so nothing
		 * reading it from another thread can take a reference to a
		 * canvas that is being destroyed. */
		obs_canvas_t *going = canvas;
		canvas = nullptr;
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
 * saved form keeps only name, uuid and flags), so restoring the mix at our
 * dimensions is this plugin's job. */
void VerticalCanvas::adopt()
{
	teardown();

	canvas = obs_get_canvas_by_name(kCanvasName);
	/* A replacement that was cut short leaves the canvas under its
	 * retiring name; it is picked up and the replacement finished. */
	if (!canvas)
		canvas = obs_get_canvas_by_name(kRetiredCanvasName);
	if (canvas) {
		if (obs_canvas_get_flags(canvas) & MIX_AUDIO)
			canvas = rebuiltWithoutAudioMix(canvas);
		ensureVideo();
		reconcileScenes();
		hookCurrentTransition();
		showCurrentScene();
	}

	/* Emitted with or without a canvas: the dock's default-on behavior
	 * hangs off this signal after the collection loads. */
	emit changed();
}

/* Earlier releases created the canvas as a program canvas, which also mixes
 * its audio: every source shown on both canvases was summed twice into every
 * track. The flags are saved with the collection and cannot be changed in
 * place, so such a canvas is replaced once, with its scenes moved across.
 * Not while an output runs, since the old canvas may be feeding it; the
 * next idle moment does it. */
obs_canvas_t *VerticalCanvas::rebuiltWithoutAudioMix(obs_canvas_t *old)
{
	rebuildPending = obs_video_active();
	if (rebuildPending) {
		obs_log(LOG_INFO, "vertical canvas keeps mixing audio until no output is active");
		return old;
	}

	struct obs_video_info ovi;
	portraitVideoInfo(&ovi);
	obs_canvas_set_name(old, kRetiredCanvasName);
	obs_canvas_t *fresh = obs_frontend_add_canvas(kCanvasName, &ovi, kCanvasFlags);
	if (!fresh) {
		obs_canvas_set_name(old, kCanvasName);
		obs_log(LOG_WARNING, "vertical canvas could not be replaced; it keeps mixing audio");
		return old;
	}

	QVector<obs_source_t *> scenes;
	obs_canvas_enum_scenes(old, collectSceneRefs, &scenes);
	for (obs_source_t *scene : scenes) {
		obs_canvas_move_scene(obs_scene_from_source(scene), fresh);
		obs_source_release(scene);
	}
	obs_frontend_remove_canvas(old);
	obs_canvas_release(old);
	obs_frontend_save();
	obs_log(LOG_INFO, "vertical canvas replaced without audio mixing; %d scene(s) moved", (int)scenes.size());
	return fresh;
}

/* Work that had to wait for the mixes to go idle, or for a source to report
 * its size, gets another go at the moments that change either. A canvas
 * replacement is a fresh adoption, so everything hooked to the old one is
 * let go of first. */
void VerticalCanvas::retryDeferredWork()
{
	if (!canvas)
		return;
	if (rebuildPending && !obs_video_active())
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
		obs_canvas_release(canvas);
		canvas = nullptr;
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
		retryDeferredWork();
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
