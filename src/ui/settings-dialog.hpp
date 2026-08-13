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
#include "../relay-status.hpp"

class QCheckBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

/* Only what has to be local lives here: account, the disconnect protection
 * toggle, stream routing, diagnostics. Slates, billing and key management
 * belong to the web console, which keeps this dialog and the auth surface
 * small. */
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(RelayAuth *auth, RelayStatus *status, QWidget *parent = nullptr);

signals:
	void routeRequested();
	void restoreRequested();
	void signedOut();

private:
	QVBoxLayout *addCard(QVBoxLayout *root, const char *headerKey);
	void buildAccountCard(QVBoxLayout *root);
	void buildStreamingCard(QVBoxLayout *root);
	void buildProtectionCard(QVBoxLayout *root);
	void buildVerticalCard(QVBoxLayout *root);
	void buildDiagnosticsCard(QVBoxLayout *root);

	void refreshTarget();
	void loadProtection();
	QCheckBox *buildVerticalToggle();
	QString buildDiagnostics() const;

	RelayAuth *auth;
	RelayStatus *status;

	QLabel *routePill = nullptr;
	QLabel *targetLabel = nullptr;
	QLabel *routeNote = nullptr;
	QPushButton *routeButton = nullptr;
	QPushButton *restoreButton = nullptr;

	QCheckBox *protectionCheck = nullptr;
	QLabel *protectionNote = nullptr;
	bool protectionLoaded = false;
};
