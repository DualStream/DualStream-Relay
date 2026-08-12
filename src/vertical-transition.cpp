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

/* Scene transitions on the portrait canvas.
 *
 * The canvas holds a transition on its program channel rather than a scene, so
 * a scene switch animates there the same way it does in the main mix. The
 * transition is a copy of whichever one OBS actually ran: built from the id and
 * settings of the source on OBS's own program channel, and started from that
 * source's transition_start signal, so both mixes move together. Anything that
 * changes the portrait program outside a transition, such as loading a scene
 * collection, cuts instead. */

#include "vertical-canvas.hpp"

#include <string.h>

#include <obs-module.h>
#include <plugin-support.h>

#include "vertical-geometry.hpp"

namespace {

/* What OBS falls back to for a scene that names a transition override but
 * carries no duration of its own. */
const int kOverrideDefaultDuration = 300;

/* Whether a name still resolves to one of the user's transitions. OBS ignores
 * an override pointing at a transition that has since been deleted, and the
 * duration goes with it. */
bool transitionExists(const char *name)
{
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);

	bool found = false;
	for (size_t i = 0; i < list.sources.num && !found; i++) {
		const char *candidate = obs_source_get_name(list.sources.array[i]);
		found = candidate && strcmp(candidate, name) == 0;
	}

	obs_frontend_source_list_free(&list);
	return found;
}

} // namespace

void VerticalCanvas::hookCurrentTransition()
{
	obs_source_t *main = obs_get_output_source(0);
	hookMainTransition(main);
	obs_source_release(main);
}

/* Follow whatever sits on OBS's program channel. It is swapped out for a
 * per-scene override and back again, so the hook moves with it rather than
 * being taken once. */
void VerticalCanvas::hookMainTransition(obs_source_t *next)
{
	obs_source_t *previous = hookedTransition ? obs_weak_source_get_source(hookedTransition) : nullptr;
	if (previous == next) {
		obs_source_release(previous);
		return;
	}

	if (previous) {
		signal_handler_disconnect(obs_source_get_signal_handler(previous), "transition_start",
					  onMainTransitionStart, this);
		obs_source_release(previous);
	}

	if (hookedTransition) {
		obs_weak_source_release(hookedTransition);
		hookedTransition = nullptr;
	}

	if (!next)
		return;

	signal_handler_connect(obs_source_get_signal_handler(next), "transition_start", onMainTransitionStart, this);
	hookedTransition = obs_source_get_weak_source(next);
}

void VerticalCanvas::onChannelChange(void *data, calldata_t *cd)
{
	if (calldata_int(cd, "channel") != 0)
		return;

	VerticalCanvas *self = static_cast<VerticalCanvas *>(data);
	self->hookMainTransition(static_cast<obs_source_t *>(calldata_ptr(cd, "source")));
}

void VerticalCanvas::onMainTransitionStart(void *data, calldata_t *cd)
{
	VerticalCanvas *self = static_cast<VerticalCanvas *>(data);
	obs_source_t *main = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));

	/* Run here rather than queued: the signal is raised inline by
	 * obs_transition_start, and starting the portrait transition in the
	 * same call is what keeps the two mixes in step. Nothing below touches
	 * Qt. */
	self->mirrorTransition(main);
}

/* Keep the portrait transition a copy of the main one, and on the canvas
 * channel. A different type is rebuilt; the same type takes the new settings,
 * so a fade's colour or a stinger's media follows. The rebuild carries the
 * sources across the way OBS does when it swaps its own transition, so the
 * scene on screen does not blink. */
void VerticalCanvas::matchTransition(obs_source_t *main)
{
	if (!canvas || !main)
		return;

	const char *id = obs_source_get_id(main);
	if (!id)
		return;

	obs_data_t *settings = obs_source_get_settings(main);
	const char *json = obs_data_get_json(settings);
	const QString fingerprint = QString::fromUtf8(json ? json : "");

	if (transition && strcmp(obs_source_get_id(transition), id) == 0) {
		if (fingerprint != transitionSettings) {
			obs_source_update(transition, settings);
			transitionSettings = fingerprint;
		}
		obs_data_release(settings);
		return;
	}

	obs_source_t *previous = transition;
	transition = obs_source_create_private(id, obs_source_get_name(main), settings);
	obs_data_release(settings);

	if (!transition) {
		obs_log(LOG_WARNING, "portrait transition '%s' could not be created", id);
		transition = previous;
		return;
	}

	transitionSettings = fingerprint;
	obs_transition_set_size(transition, kPortraitWidth, kPortraitHeight);
	obs_transition_set_alignment(transition, OBS_ALIGN_CENTER);
	obs_transition_set_scale_type(transition, OBS_TRANSITION_SCALE_ASPECT);

	if (previous) {
		obs_transition_swap_begin(transition, previous);
		obs_canvas_set_channel(canvas, 0, transition);
		obs_transition_swap_end(transition, previous);
		obs_source_release(previous);
	} else {
		obs_canvas_set_channel(canvas, 0, transition);
	}
}

/* The duration OBS resolved for this switch: a scene naming a transition
 * override brings its own, everything else takes the transition bar's. */
int VerticalCanvas::mirroredDuration(obs_source_t *destScene) const
{
	if (destScene) {
		obs_data_t *priv = obs_source_get_private_settings(destScene);
		const char *name = obs_data_get_string(priv, "transition");
		int duration = 0;

		if (name && *name && transitionExists(name)) {
			obs_data_set_default_int(priv, "transition_duration", kOverrideDefaultDuration);
			duration = (int)obs_data_get_int(priv, "transition_duration");
		}

		obs_data_release(priv);
		if (duration > 0)
			return duration;
	}

	return obs_frontend_get_transition_duration();
}

void VerticalCanvas::mirrorTransition(obs_source_t *main)
{
	if (!canvas || !main)
		return;

	matchTransition(main);
	if (!transition)
		return;

	/* Destination read off the transition rather than the frontend: in
	 * studio mode the current scene is still the preview one at this point,
	 * and with scene duplication on, the real destination is a private copy
	 * carrying the same name. A fade to black has no destination at all,
	 * and NULL fades the portrait mix out with it. */
	obs_source_t *dest = obs_transition_get_source(main, OBS_TRANSITION_SOURCE_B);
	obs_source_t *counterpart = counterpartOf(dest);

	obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, (uint32_t)mirroredDuration(dest), counterpart);

	obs_source_release(counterpart);
	obs_source_release(dest);
}

/* Put the counterpart of the current landscape scene on the canvas with no
 * animation. This is for the moments with no transition to follow, such as
 * adopting a canvas or loading a collection, and as the backstop once a
 * transition ends: a manual t-bar drag has no duration to mirror, so the
 * portrait mix is simply held to the same result. */
void VerticalCanvas::showCurrentScene()
{
	if (!canvas)
		return;

	obs_source_t *main = obs_get_output_source(0);
	if (main) {
		matchTransition(main);
		obs_source_release(main);
	}

	obs_source_t *counterpart = currentCounterpart();

	if (transition) {
		obs_source_t *active = obs_transition_get_active_source(transition);
		const bool settled = active == counterpart;
		obs_source_release(active);
		if (!settled)
			obs_transition_set(transition, counterpart);
	} else {
		obs_canvas_set_channel(canvas, 0, counterpart);
	}

	obs_source_release(counterpart);
}

void VerticalCanvas::releaseTransition()
{
	hookMainTransition(nullptr);

	if (!transition)
		return;

	if (canvas)
		obs_canvas_set_channel(canvas, 0, nullptr);

	obs_source_release(transition);
	transition = nullptr;
	transitionSettings.clear();
}
