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

#include "vertical-common.hpp"

#include <QMainWindow>
#include <QWidget>

#include <obs-frontend-api.h>

#include "../vertical-canvas.hpp"
#include "dsr-ui-common.hpp"

const char *kVerticalOffFlag = "vertical_off";

namespace {

struct FindById {
	int64_t id;
	obs_sceneitem_t *found;
};

bool findByIdCb(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	FindById *ctx = static_cast<FindById *>(param);
	if (obs_sceneitem_get_id(item) == ctx->id) {
		obs_sceneitem_addref(item);
		ctx->found = item;
		return false;
	}
	return true;
}

} // namespace

/* obs_display_create and obs_display_set_background_color take 0xAABBGGRR,
 * the order the frontend's own color_to_int produces. */
uint32_t dsrPreviewSurroundColor()
{
	return 0xFF1A1413;
}

QColor dsrObsPreviewBackground()
{
	QMainWindow *main = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (main) {
		for (QWidget *child : main->findChildren<QWidget *>()) {
			if (qstrcmp(child->metaObject()->className(), "OBSQTDisplay") != 0)
				continue;
			const QColor colour = child->property("displayBackgroundColor").value<QColor>();
			if (colour.isValid())
				return colour;
		}
	}
	/* GREY_COLOR_BACKGROUND, the value every theme starts from. */
	return QColor::fromRgb(0x4C, 0x4C, 0x4C);
}

obs_sceneitem_t *dsrFindCounterpartItem(VerticalCanvas *manager, int64_t itemId)
{
	obs_source_t *sceneSource = manager->currentCounterpart();
	if (!sceneSource)
		return nullptr;

	FindById ctx = {itemId, nullptr};
	obs_scene_enum_items(obs_scene_from_source(sceneSource), findByIdCb, &ctx);
	obs_source_release(sceneSource);
	return ctx.found;
}

bool dsrCollectSceneItems(obs_scene_t *, obs_sceneitem_t *item, void *param)
{
	QVector<obs_sceneitem_t *> *items = static_cast<QVector<obs_sceneitem_t *> *>(param);
	obs_sceneitem_addref(item);
	items->append(item);
	return true;
}

QString dsrVerticalStyleSheet()
{
	return dsrSharedStyleSheet() + QStringLiteral(R"(
#vertRow {
	border-radius: 10px;
	background: rgba(255, 255, 255, 10);
}
#vertRow:hover { background: rgba(255, 255, 255, 20); }
#vertRow[selected="true"] { background: rgba(8, 142, 188, 40); }
#vertName { font-weight: 500; }
#dropLine { background: #F3490E; border-radius: 1px; }
)");
}
