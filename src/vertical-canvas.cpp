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

VerticalCanvas *singleton = nullptr;

void portraitVideoInfo(struct obs_video_info *ovi)
{
	/* Frame rate, format and color space follow the profile; only the
	 * geometry is ours. Composited at delivery size, so no scaling pass
	 * sits between the canvas and the encoder. */
	obs_get_video_info(ovi);
	ovi->base_width = kPortraitWidth;
	ovi->base_height = kPortraitHeight;
	ovi->output_width = kPortraitWidth;
	ovi->output_height = kPortraitHeight;
}

} // namespace

VerticalCanvas *VerticalCanvas::instance()
{
	return singleton;
}

VerticalCanvas::VerticalCanvas(QObject *parent) : QObject(parent)
{
	singleton = this;
}

VerticalCanvas::~VerticalCanvas()
{
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

obs_source_t *VerticalCanvas::currentCounterpart() const
{
	if (!canvas)
		return nullptr;

	obs_source_t *current = obs_frontend_get_current_scene();
	if (!current)
		return nullptr;

	obs_scene_t *counterpart = obs_canvas_get_scene_by_name(canvas, obs_source_get_name(current));
	obs_source_release(current);
	return counterpart ? obs_scene_get_source(counterpart) : nullptr;
}

void VerticalCanvas::setEnabled(bool on)
{
	if (on == enabled())
		return;

	if (on) {
		struct obs_video_info ovi;
		portraitVideoInfo(&ovi);
		canvas = obs_frontend_add_canvas(kCanvasName, &ovi, PROGRAM);
		if (!canvas) {
			obs_log(LOG_ERROR, "vertical canvas could not be created");
			return;
		}
		reconcileScenes();
		retargetChannel();
		obs_log(LOG_INFO, "vertical canvas enabled");
	} else {
		setSelectedItemId(-1);
		stopOutput(true);
		releaseOutput();
		disconnectAllSceneSignals();
		obs_frontend_remove_canvas(canvas);
		obs_canvas_release(canvas);
		canvas = nullptr;
		obs_log(LOG_INFO, "vertical canvas disabled");
	}

	obs_frontend_save();
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
}

/* Pick up a canvas the scene collection already carries. The frontend
 * recreates it at collection load, but deliberately without a video mix (the
 * saved form keeps only name, uuid and flags), so restoring the mix at our
 * dimensions is this plugin's job. */
void VerticalCanvas::adopt()
{
	teardown();

	canvas = obs_get_canvas_by_name(kCanvasName);
	if (canvas) {
		ensureVideo();
		reconcileScenes();
		retargetChannel();
	}

	/* Emitted with or without a canvas: the dock's default-on behavior
	 * hangs off this signal after the collection loads. */
	emit changed();
}

void VerticalCanvas::teardown()
{
	setSelectedItemId(-1);
	stopOutput(true);
	releaseOutput();
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

/* Point the canvas at the portrait counterpart of whatever landscape scene is
 * live, so a scene switch carries across. */
void VerticalCanvas::retargetChannel()
{
	if (!canvas)
		return;

	obs_source_t *counterpart = currentCounterpart();
	obs_canvas_set_channel(canvas, 0, counterpart);
	obs_source_release(counterpart);
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
		/* The selection names an item of the outgoing scene. */
		setSelectedItemId(-1);
		retargetChannel();
		emit changed();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		reconcileScenes();
		retargetChannel();
		emit changed();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		maybeStartOutput();
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
