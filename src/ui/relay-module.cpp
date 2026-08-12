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

/* Module registration: the docks, the frontend event fan-out, and the
 * end-stream hotkey. */

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QMetaObject>

#include "../vertical-canvas.hpp"
#include "relay-dock.hpp"
#include "vertical-dock.hpp"
#include "vertical-sources-dock.hpp"

static RelayDock *dockInstance = nullptr;
static VerticalCanvas *verticalManager = nullptr;
static obs_hotkey_id endHotkeyId = OBS_INVALID_HOTKEY_ID;

static void frontend_event_cb(enum obs_frontend_event event, void *)
{
	/* The manager first: the relay dock reads its state, and on teardown
	 * events the manager must let go of libobs objects before anything
	 * else reacts. */
	if (verticalManager)
		verticalManager->handleFrontendEvent(event);
	if (dockInstance)
		dockInstance->handleFrontendEvent(event);
}

static void tools_menu_cb(void *)
{
	if (dockInstance)
		dockInstance->showDockWindow();
}

static void end_hotkey_cb(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && dockInstance)
		QMetaObject::invokeMethod(dockInstance, "endStreamHotkey", Qt::QueuedConnection);
}

extern "C" bool dsr_frontend_init(void)
{
	verticalManager = new VerticalCanvas();

	dockInstance = new RelayDock();
	if (!obs_frontend_add_dock_by_id("dsr_relay_dock", obs_module_text("Dock.Title"), dockInstance)) {
		delete dockInstance;
		dockInstance = nullptr;
		delete verticalManager;
		verticalManager = nullptr;
		return false;
	}

	VerticalDock *verticalDock = new VerticalDock(verticalManager);
	if (!obs_frontend_add_dock_by_id("dsr_vertical_dock", obs_module_text("Vertical.DockTitle"), verticalDock))
		delete verticalDock;

	VerticalSourcesDock *verticalSources = new VerticalSourcesDock(verticalManager);
	if (!obs_frontend_add_dock_by_id("dsr_vertical_sources_dock", obs_module_text("Vertical.SourcesTitle"),
					 verticalSources))
		delete verticalSources;

	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
	obs_frontend_add_tools_menu_item(obs_module_text("Menu.ShowDock"), tools_menu_cb, nullptr);
	endHotkeyId = obs_hotkey_register_frontend("dsr_end_stream", obs_module_text("Hotkey.EndStream"), end_hotkey_cb,
						   nullptr);
	return true;
}

extern "C" void dsr_frontend_shutdown(void)
{
	if (endHotkeyId != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(endHotkeyId);
		endHotkeyId = OBS_INVALID_HOTKEY_ID;
	}
	obs_frontend_remove_event_callback(frontend_event_cb, nullptr);
	/* The dock widgets belong to the OBS main window and are destroyed
	 * with it. The manager holds libobs references, so it goes now. */
	delete verticalManager;
	verticalManager = nullptr;
	dockInstance = nullptr;
}
