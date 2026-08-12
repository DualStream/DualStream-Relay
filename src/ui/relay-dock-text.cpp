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

/* Display text and the dock style sheet. */

#include "relay-dock-text.hpp"

#include "dsr-ui-common.hpp"

const char *kFirstRunFlag = "first_run_done";

QString dsrRelayStyleSheet()
{
	/* The DualStream design language, translated to Qt style sheets:
	 * glass surfaces over the host theme, brand orange for the one primary
	 * action per view, teal for ready and protected, live orange for live.
	 * Only surfaces and accents are set; text and window colors inherit the
	 * active OBS theme so the dock is native in all six. */
	return dsrSharedStyleSheet() + QStringLiteral(R"(
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

#timerLabel { color: palette(text); font-size: 12px; }

#banner {
	border-radius: 8px;
	padding: 9px 11px;
	font-size: 12px;
}
#banner[kind="protect"] { color: #33A9D1; background: rgba(8, 142, 188, 33); }
#banner[kind="warn"]    { color: #E8981C; background: rgba(245, 158, 11, 30); }
#banner[kind="error"]   { color: #F87171; background: rgba(239, 68, 68, 28); }

#destRow {
	border-radius: 10px;
	background: rgba(255, 255, 255, 10);
}
#destRow:hover { background: rgba(255, 255, 255, 20); }
#destName { font-weight: 500; }
#destState[state="live"]         { color: #4ADE80; font-weight: 600; }
#destState[state="connecting"]   { color: #E8981C; }
#destState[state="reconnecting"] { color: #E8981C; }
#destState[state="rejected"]     { color: #F87171; font-weight: 600; }
#destState[state="ended"]        { color: palette(text); }
#destError { color: #F87171; font-size: 11px; }
#canvasBadge {
	color: palette(text);
	font-size: 11px;
	padding: 1px 7px;
	border-radius: 8px;
	background: rgba(128, 128, 128, 40);
}
#mutedText { color: palette(text); font-size: 12px; }
#bodyText { font-size: 13px; }

/* The platform mark is painted, not styled: the artwork is a white
 * silhouette recolored per brand, which a style sheet cannot express. */

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

#secondaryButton {
	background: rgba(255, 255, 255, 16);
	color: palette(text);
	border: none;
	border-radius: 8px;
	padding: 7px 14px;
	font-weight: 500;
	font-size: 12px;
}
#secondaryButton:hover   { background: rgba(255, 255, 255, 30); }
#secondaryButton:pressed { background: rgba(255, 255, 255, 42); }

#endButton {
	background: rgba(239, 68, 68, 26);
	color: #F87171;
	border: none;
	border-radius: 8px;
	padding: 7px 14px;
	font-weight: 500;
	font-size: 12px;
}
#endButton:hover   { background: rgba(239, 68, 68, 48); }
#endButton:pressed { background: rgba(239, 68, 68, 66); }

#codeLabel { letter-spacing: 3px; font-weight: 700; }

QScrollArea { background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: rgba(128, 128, 128, 90); border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: rgba(128, 128, 128, 130); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)");
}
