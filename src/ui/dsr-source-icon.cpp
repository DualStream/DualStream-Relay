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

#include "dsr-source-icon.hpp"

#include <QCheckBox>
#include <QSizePolicy>
#include <QWidget>

#include <obs-frontend-api.h>

namespace {

/* Property names the theme assigns the icons to, in the same order
 * OBSBasic::GetSourceIcon resolves them. */
const char *iconProperty(enum obs_icon_type type)
{
	switch (type) {
	case OBS_ICON_TYPE_IMAGE:
		return "imageIcon";
	case OBS_ICON_TYPE_COLOR:
		return "colorIcon";
	case OBS_ICON_TYPE_SLIDESHOW:
		return "slideshowIcon";
	case OBS_ICON_TYPE_AUDIO_INPUT:
		return "audioInputIcon";
	case OBS_ICON_TYPE_AUDIO_OUTPUT:
		return "audioOutputIcon";
	case OBS_ICON_TYPE_DESKTOP_CAPTURE:
		return "desktopCapIcon";
	case OBS_ICON_TYPE_WINDOW_CAPTURE:
		return "windowCapIcon";
	case OBS_ICON_TYPE_GAME_CAPTURE:
		return "gameCapIcon";
	case OBS_ICON_TYPE_CAMERA:
		return "cameraIcon";
	case OBS_ICON_TYPE_TEXT:
		return "textIcon";
	case OBS_ICON_TYPE_MEDIA:
		return "mediaIcon";
	case OBS_ICON_TYPE_BROWSER:
		return "browserIcon";
	case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT:
		return "audioProcessOutputIcon";
	default:
		return "defaultIcon";
	}
}

QCheckBox *makeIndicator(const char *classes)
{
	QCheckBox *box = new QCheckBox;
	box->setProperty("class", QString::fromUtf8(classes));
	box->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
#ifdef __APPLE__
	box->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
	return box;
}

} // namespace

QIcon dsrSourceIcon(enum obs_icon_type type)
{
	QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!main)
		return QIcon();
	return main->property(iconProperty(type)).value<QIcon>();
}

QCheckBox *dsrMakeVisibilityCheck()
{
	return makeIndicator("checkbox-icon indicator-visibility");
}

QCheckBox *dsrMakeLockCheck()
{
	return makeIndicator("checkbox-icon indicator-lock");
}
