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

#pragma once

#include <QDialog>

#include "../relay-auth.hpp"
#include "../relay-destinations.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

/* Add or edit one relay destination. Connected accounts come first because
 * one click there beats pasting a stream key. Twitch and Kick lock the
 * canvas selector to landscape; the relay cannot deliver portrait to either
 * platform and a destination configured that way would never go live. */
class DestinationDialog : public QDialog {
	Q_OBJECT

public:
	/* Add mode. */
	DestinationDialog(RelayAuth *auth, RelayDestinations *destinations, QWidget *parent = nullptr);
	/* Edit mode. */
	DestinationDialog(RelayAuth *auth, RelayDestinations *destinations, const DsrDestination &existing,
			  QWidget *parent = nullptr);

private:
	void buildAddUi();
	void buildEditUi();
	void loadSuggestions();
	void submitCustom();
	void submitEdit();
	void showError(const QString &errorKey);

	static QStringList allowedCanvases(const QString &platform);
	static void fillCanvasCombo(QComboBox *combo, const QString &platform, const QString &selected, QLabel *note);

	RelayAuth *auth;
	RelayDestinations *destinations;

	bool editMode = false;
	DsrDestination existing;

	QVBoxLayout *suggestionLayout = nullptr;
	QLabel *suggestionEmptyLabel = nullptr;
	QCheckBox *termsCheck = nullptr;
	QLabel *errorLabel = nullptr;

	QComboBox *presetCombo = nullptr;
	QLineEdit *labelEdit = nullptr;
	QLineEdit *serverEdit = nullptr;
	QLineEdit *keyEdit = nullptr;
	QComboBox *canvasCombo = nullptr;
	QLabel *canvasNote = nullptr;
	QPushButton *customAddButton = nullptr;

	QLineEdit *ytLandscapeTitle = nullptr;
	QLineEdit *ytPortraitTitle = nullptr;
	QPlainTextEdit *ytDescription = nullptr;
	QComboBox *ytPrivacy = nullptr;
};
