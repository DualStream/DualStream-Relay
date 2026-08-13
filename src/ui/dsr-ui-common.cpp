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

#include "dsr-ui-common.hpp"

#include <QFile>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSaveFile>
#include <QStyle>
#include <QWidget>

#include <obs-module.h>
#include <util/platform.h>

namespace {

QString settingsFilePath()
{
	char *path = obs_module_config_path("settings.json");
	if (!path)
		return QString();
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

} // namespace

QString dsrText(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString dsrCanvasDisplay(const QString &canvas)
{
	if (canvas == QLatin1String("portrait"))
		return dsrText("Canvas.Portrait");
	if (canvas == QLatin1String("both"))
		return dsrText("Canvas.Both");
	return dsrText("Canvas.Landscape");
}

QFrame *dsrMakeSeparator()
{
	QFrame *line = new QFrame;
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	return line;
}

QLabel *dsrMakeSectionHeader(const char *key)
{
	QLabel *label = new QLabel(dsrText(key));
	label->setStyleSheet(QStringLiteral("font-weight: 600;"));
	return label;
}

void dsrRepolish(QWidget *widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

bool dsrReadFlag(const char *key)
{
	QFile file(settingsFilePath());
	if (!file.open(QIODevice::ReadOnly))
		return false;
	return QJsonDocument::fromJson(file.readAll()).object().value(QString::fromUtf8(key)).toBool();
}

void dsrWriteFlag(const char *key, bool value)
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}

	QJsonObject obj;
	QFile existing(settingsFilePath());
	if (existing.open(QIODevice::ReadOnly)) {
		obj = QJsonDocument::fromJson(existing.readAll()).object();
		existing.close();
	}
	obj.insert(QString::fromUtf8(key), value);

	QSaveFile file(settingsFilePath());
	if (!file.open(QIODevice::WriteOnly))
		return;
	file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
	file.commit();
}

QString dsrSharedStyleSheet()
{
	return QStringLiteral(R"(
#mutedText { color: palette(text); font-size: 12px; }
#bodyText { font-size: 13px; }

/* Secondary text: a fixed grey rather than a palette color, because it has to
 * stay quieter than body text on light and dark OBS themes alike. */
#settingsNote { color: #8A8A8A; font-size: 11px; }
#settingsValue { font-size: 12px; }
#monoValue { font-family: monospace; font-size: 11px; color: #8A8A8A; }

/* State of the thing named beside it: the dock's header, a routing state. */
#statusPill {
	border-radius: 11px;
	padding: 3px 12px;
	font-weight: 600;
	font-size: 12px;
	color: palette(text);
	background: rgba(128, 128, 128, 38);
}
#statusPill[state="live"]      { color: #F75B1E; background: rgba(247, 91, 30, 40); }
#statusPill[state="ready"]     { color: #33A9D1; background: rgba(8, 142, 188, 40); }
#statusPill[state="protected"] { color: #33A9D1; background: rgba(8, 142, 188, 40); }
#statusPill[state="warn"]      { color: #E8981C; background: rgba(245, 158, 11, 38); }
#statusPill[state="error"]     { color: #F87171; background: rgba(239, 68, 68, 38); }

/* One rounded surface per topic in a settings dialog. */
#settingsCard {
	border-radius: 10px;
	background: rgba(255, 255, 255, 10);
}

/* One row shape for anything that names a platform: the destination rows in
 * the dock and the connected-account rows in the add dialog. */
#destRow, #accountRow {
	border-radius: 10px;
	background: rgba(255, 255, 255, 10);
}
#accountRow[added="true"] { background: rgba(255, 255, 255, 6); }
#destName { font-weight: 500; }
#canvasBadge {
	color: palette(text);
	font-size: 11px;
	padding: 1px 7px;
	border-radius: 8px;
	background: rgba(128, 128, 128, 40);
}

#primaryButton {
	background-color: #F3490E;
	color: #FFFFFF;
	border: none;
	border-radius: 8px;
	padding: 8px 16px;
	font-weight: 500;
	font-size: 13px;
}
#primaryButton:hover   { background-color: #FF5A1F; }
#primaryButton:pressed { background-color: #D63F0C; }
#primaryButton:disabled { background-color: rgba(243, 73, 14, 80); color: rgba(255, 255, 255, 120); }

QScrollArea { background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: rgba(128, 128, 128, 90); border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: rgba(128, 128, 128, 130); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)");
}
