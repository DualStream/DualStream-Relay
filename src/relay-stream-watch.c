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

/* What the profile's streaming output is doing, taken from the output
 * itself rather than from the frontend's summary of it.
 *
 * Two things are read here, and neither is available any other way. The
 * first is why a stream ended: the frontend raises events for stopping and
 * stopped, but neither carries the stop code or the transport's error text,
 * and by the time the dock reacts the output may already be gone.
 *
 * The second is whether the output currently holds the streaming service.
 * That matters because the service must not be rewritten underneath it, and
 * the frontend's own answer is no help: it reports a stream active only once
 * the connection has succeeded, so the whole connect attempt looks idle.
 * The output's signals are exact. It emits "starting" once it has taken the
 * service (or "start" when a stream delay is configured, where the first
 * start is announced by the capture beginning), and "stop" when it is
 * finished with it. A start that fails before either emits neither, which
 * is the right answer: nothing ever took the service. */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/threading.h>
#include <plugin-support.h>

#include "relay-output.h"

static obs_output_t *watched;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool engaged;
static bool have_stop;
static int stop_code;
static char *stop_error;

/* All three run on the output's own thread; they keep to copying facts out. */

static void on_output_engage(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(cd);
	pthread_mutex_lock(&state_mutex);
	engaged = true;
	pthread_mutex_unlock(&state_mutex);
}

static void on_output_stop(void *data, calldata_t *cd)
{
	UNUSED_PARAMETER(data);
	const long long code = calldata_int(cd, "code");
	obs_output_t *output = calldata_ptr(cd, "output");
	const char *error = output ? obs_output_get_last_error(output) : NULL;

	pthread_mutex_lock(&state_mutex);
	engaged = false;
	stop_code = (int)code;
	have_stop = code != OBS_OUTPUT_SUCCESS;
	bfree(stop_error);
	stop_error = (error && *error) ? bstrdup(error) : NULL;
	pthread_mutex_unlock(&state_mutex);
}

static void connect_signals(obs_output_t *output, bool connect)
{
	signal_handler_t *handler = obs_output_get_signal_handler(output);
	if (!handler)
		return;

	if (connect) {
		signal_handler_connect(handler, "starting", on_output_engage, NULL);
		signal_handler_connect(handler, "start", on_output_engage, NULL);
		signal_handler_connect(handler, "stop", on_output_stop, NULL);
	} else {
		signal_handler_disconnect(handler, "starting", on_output_engage, NULL);
		signal_handler_disconnect(handler, "start", on_output_engage, NULL);
		signal_handler_disconnect(handler, "stop", on_output_stop, NULL);
	}
}

void dsr_stream_watch_arm(void)
{
	obs_output_t *output = obs_frontend_get_streaming_output();
	if (!output)
		return;
	if (output == watched) {
		obs_output_release(output);
		return;
	}

	dsr_stream_watch_disarm();
	connect_signals(output, true);
	/* The reference from the frontend is kept until disarm, so the signal
	 * handlers are never left connected to a destroyed output. */
	watched = output;
}

void dsr_stream_watch_disarm(void)
{
	if (!watched)
		return;
	connect_signals(watched, false);
	obs_output_release(watched);
	watched = NULL;

	/* Nothing is being watched, so nothing can report finishing with the
	 * service. Anything that did engage has been disconnected from. */
	pthread_mutex_lock(&state_mutex);
	engaged = false;
	pthread_mutex_unlock(&state_mutex);
}

bool dsr_stream_output_engaged(void)
{
	pthread_mutex_lock(&state_mutex);
	const bool value = engaged;
	pthread_mutex_unlock(&state_mutex);
	return value;
}

void dsr_stream_watch_clear(void)
{
	pthread_mutex_lock(&state_mutex);
	have_stop = false;
	stop_code = OBS_OUTPUT_SUCCESS;
	bfree(stop_error);
	stop_error = NULL;
	pthread_mutex_unlock(&state_mutex);
}

bool dsr_stream_last_stop(int *code, char **error)
{
	pthread_mutex_lock(&state_mutex);
	if (!have_stop) {
		pthread_mutex_unlock(&state_mutex);
		return false;
	}
	if (code)
		*code = stop_code;
	if (error)
		*error = stop_error ? bstrdup(stop_error) : NULL;
	pthread_mutex_unlock(&state_mutex);
	return true;
}
