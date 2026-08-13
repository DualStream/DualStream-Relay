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

/* Add mode: the connected accounts list and the custom RTMP form. Accounts
 * already in use are listed too rather than hidden, so the section answers
 * both questions a user has here, what can I add and what did I already add. */

#include "destination-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include "../relay-secrets.hpp"
#include "dsr-platform-mark.hpp"
#include "dsr-ui-common.hpp"

namespace {

/* Published ingest endpoints for the platforms that hand out a stream key
 * rather than an OAuth connection. Same values the DualStream desktop app
 * uses, so a destination set up in either place points at the same host. The
 * field stays editable: these are stable but not guaranteed, and a user given
 * a different one by the platform has to be able to use it. */
QString presetServer(const QString &preset)
{
	if (preset == QLatin1String("tiktok"))
		return QStringLiteral("rtmp://push.live.tiktok.com/live/");
	if (preset == QLatin1String("facebook") || preset == QLatin1String("facebook_reels"))
		return QStringLiteral("rtmps://live-api-s.facebook.com:443/rtmp/");
	return QString();
}

/* The orientation each preset actually delivers. */
QString presetCanvas(const QString &preset)
{
	if (preset == QLatin1String("tiktok") || preset == QLatin1String("facebook_reels"))
		return QStringLiteral("portrait");
	return QStringLiteral("landscape");
}

} // namespace

void DestinationDialog::buildAddUi()
{
	setMinimumWidth(420);
	/* The dialog carries the shared rules itself rather than relying on
	 * inheriting them from whichever dock opened it. */
	setStyleSheet(dsrSharedStyleSheet());
	QVBoxLayout *layout = new QVBoxLayout(this);

	QHBoxLayout *accountsHeader = new QHBoxLayout;
	accountsHeader->addWidget(dsrMakeSectionHeader("Destinations.ConnectedAccounts"));
	accountsHeader->addStretch();
	/* Connecting a platform is an account-level action the plugin cannot do
	 * for the user, so the section that depends on it says where to go. */
	QPushButton *manageAccounts = new QPushButton(dsrText("Destinations.ManageAccounts"));
	manageAccounts->setObjectName(QStringLiteral("secondaryButton"));
	manageAccounts->setCursor(Qt::PointingHandCursor);
	connect(manageAccounts, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl(auth->webUrl(QStringLiteral("/account")))); });
	accountsHeader->addWidget(manageAccounts);
	layout->addLayout(accountsHeader);

	QWidget *suggestionHost = new QWidget;
	suggestionLayout = new QVBoxLayout(suggestionHost);
	suggestionLayout->setContentsMargins(0, 0, 0, 0);
	suggestionEmptyLabel = new QLabel(dsrText("Destinations.LoadingAccounts"));
	suggestionEmptyLabel->setObjectName(QStringLiteral("mutedText"));
	suggestionEmptyLabel->setWordWrap(true);
	suggestionLayout->addWidget(suggestionEmptyLabel);
	layout->addWidget(suggestionHost);

	layout->addWidget(dsrMakeSeparator());
	layout->addWidget(dsrMakeSectionHeader("Destinations.CustomRtmp"));

	QFormLayout *form = new QFormLayout;
	presetCombo = new QComboBox;
	presetCombo->addItem(dsrText("Destinations.Preset.Generic"), QString());
	presetCombo->addItem(dsrText("Destinations.Preset.TikTok"), QStringLiteral("tiktok"));
	presetCombo->addItem(dsrText("Destinations.Preset.Facebook"), QStringLiteral("facebook"));
	presetCombo->addItem(dsrText("Destinations.Preset.FacebookReels"), QStringLiteral("facebook_reels"));
	form->addRow(dsrText("Destinations.Platform"), presetCombo);

	labelEdit = new QLineEdit;
	form->addRow(dsrText("Destinations.Label"), labelEdit);

	serverEdit = new QLineEdit;
	serverEdit->setPlaceholderText(QStringLiteral("rtmp://"));
	form->addRow(dsrText("Destinations.Server"), serverEdit);

	keyEdit = new QLineEdit;
	keyEdit->setEchoMode(QLineEdit::Password);
	form->addRow(dsrText("Destinations.StreamKey"), keyEdit);

	canvasCombo = new QComboBox;
	canvasNote = new QLabel;
	canvasNote->setWordWrap(true);
	canvasNote->setVisible(false);
	fillCanvasCombo(canvasCombo, QString(), QStringLiteral("landscape"), canvasNote);
	form->addRow(dsrText("Destinations.Canvas"), canvasCombo);
	layout->addLayout(form);
	layout->addWidget(canvasNote);

	connect(presetCombo, &QComboBox::currentIndexChanged, this, &DestinationDialog::applyPreset);

	customAddButton = new QPushButton(dsrText("Destinations.Add"));
	customAddButton->setObjectName(QStringLiteral("primaryButton"));
	connect(customAddButton, &QPushButton::clicked, this, &DestinationDialog::submitCustom);
	QHBoxLayout *customButtonRow = new QHBoxLayout;
	customButtonRow->addStretch();
	customButtonRow->addWidget(customAddButton);
	layout->addLayout(customButtonRow);

	layout->addWidget(dsrMakeSeparator());

	termsCheck = new QCheckBox(dsrText("Destinations.TermsAck"));
	layout->addWidget(termsCheck);

	errorLabel = new QLabel;
	errorLabel->setWordWrap(true);
	errorLabel->setStyleSheet(QStringLiteral("color: #EF4444;"));
	errorLabel->setVisible(false);
	layout->addWidget(errorLabel);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	buttons->button(QDialogButtonBox::Close)->setText(dsrText("Button.Close"));
	layout->addWidget(buttons);
}

/* Fill in what the chosen preset already determines: its ingest host, its
 * orientation, and the name it will appear under. Only the stream key is left,
 * because only the user's account can supply it. Anything typed over a filled
 * field is kept; the fields are prefilled, not owned. */
void DestinationDialog::applyPreset()
{
	const QString preset = presetCombo->currentData().toString();
	const QString server = presetServer(preset);

	/* Replaced only while it still holds a value this put there, so a
	 * server the user pasted survives a change of mind about the preset. */
	if (serverEdit->text().trimmed().isEmpty() || serverEdit->text() == filledServer)
		serverEdit->setText(server);
	filledServer = server;

	/* A placeholder rather than text: an empty label already renders as the
	 * platform name, and prefilling it would make every destination on the
	 * platform arrive with the same one. */
	labelEdit->setPlaceholderText(dsrPlatformName(preset));

	fillCanvasCombo(canvasCombo, QString(),
			preset.isEmpty() ? canvasCombo->currentData().toString() : presetCanvas(preset), canvasNote);
}

/* Same shape as a destination row in the dock: brand mark, account name, then
 * what you can do with it. An account already in use keeps its place with an
 * Added badge instead of controls, since leaving those out is what made the
 * section look empty to somebody who had connected everything. */
QWidget *DestinationDialog::makeAccountRow(const DsrSuggestion &suggestion)
{
	const bool added = suggestion.alreadyAdded;

	QWidget *row = new QWidget(this);
	row->setObjectName(QStringLiteral("accountRow"));
	row->setProperty("added", added);
	QHBoxLayout *rowLayout = new QHBoxLayout(row);
	rowLayout->setContentsMargins(10, 6, 8, 6);
	rowLayout->setSpacing(9);

	QLabel *mark = new QLabel;
	mark->setPixmap(dsrPlatformMark(dsrPlatformKey(suggestion.platform), 28, devicePixelRatioF()));
	mark->setFixedSize(28, 28);
	rowLayout->addWidget(mark);

	QLabel *name = new QLabel(suggestionName(suggestion));
	name->setObjectName(added ? QStringLiteral("mutedText") : QStringLiteral("destName"));
	rowLayout->addWidget(name, 1);

	if (added) {
		QLabel *badge = new QLabel(dsrText("Destinations.Added"));
		badge->setObjectName(QStringLiteral("canvasBadge"));
		rowLayout->addWidget(badge);
		return row;
	}

	QComboBox *canvas = new QComboBox;
	fillCanvasCombo(canvas, suggestion.platform,
			suggestion.suggestedCanvas.isEmpty() ? QStringLiteral("landscape")
							     : suggestion.suggestedCanvas,
			nullptr);
	rowLayout->addWidget(canvas);

	/* Secondary here, primary on the custom form: with a row per account the
	 * accent would stop meaning anything if every one of them wore it. */
	QPushButton *add = new QPushButton(dsrText("Destinations.Add"));
	add->setObjectName(QStringLiteral("secondaryButton"));
	add->setCursor(Qt::PointingHandCursor);
	rowLayout->addWidget(add);

	QPointer<DestinationDialog> self(this);
	const QString platform = suggestion.platform;
	const QString accountId = suggestion.accountId;
	connect(add, &QPushButton::clicked, this, [self, platform, accountId, canvas, add]() {
		if (!self)
			return;
		if (!self->termsCheck->isChecked()) {
			self->showError(QStringLiteral("Error.TermsRequired"));
			return;
		}
		add->setEnabled(false);

		QJsonObject body;
		body.insert(QStringLiteral("type"), QStringLiteral("oauth"));
		body.insert(QStringLiteral("platform"), platform);
		body.insert(QStringLiteral("connected_account_id"), accountId);
		body.insert(QStringLiteral("canvas"), canvas->currentData().toString());
		body.insert(QStringLiteral("terms_ack"), true);

		QPointer<QPushButton> addGuard(add);
		self->destinations->create(body, [self, addGuard](bool ok, QString errorKey, QString) {
			if (!self)
				return;
			if (ok) {
				if (addGuard)
					addGuard->setText(dsrText("Destinations.Added"));
			} else {
				if (addGuard)
					addGuard->setEnabled(true);
				self->showError(errorKey);
			}
		});
	});

	return row;
}

/* The account name on its own. The logo already says which platform it is, so
 * the "<platform> - " the server puts in front of the label comes off, exactly
 * as it does on the dock's own rows. */
QString DestinationDialog::suggestionName(const DsrSuggestion &suggestion)
{
	const QString label = dsrStripPlatformPrefix(suggestion.label, suggestion.platform);
	if (!label.isEmpty())
		return label;
	if (!suggestion.username.isEmpty())
		return suggestion.username;
	return dsrPlatformName(suggestion.platform);
}

void DestinationDialog::loadSuggestions()
{
	QPointer<DestinationDialog> self(this);
	destinations->fetchDiscover([self](bool ok, QVector<DsrSuggestion> suggestions) {
		if (!self)
			return;
		DestinationDialog *dlg = self.data();

		if (!ok) {
			dlg->suggestionEmptyLabel->setText(dsrText("Destinations.AccountsUnavailable"));
			return;
		}

		if (suggestions.isEmpty()) {
			dlg->suggestionEmptyLabel->setText(dsrText("Destinations.NoAccounts"));
			return;
		}

		/* Anything addable first; the rest is reference. */
		std::stable_sort(suggestions.begin(), suggestions.end(),
				 [](const DsrSuggestion &a, const DsrSuggestion &b) {
					 return !a.alreadyAdded && b.alreadyAdded;
				 });

		const bool anyAddable = !suggestions.first().alreadyAdded;
		if (anyAddable)
			dlg->suggestionEmptyLabel->setVisible(false);
		else
			dlg->suggestionEmptyLabel->setText(dsrText("Destinations.AllAccountsAdded"));

		for (const DsrSuggestion &suggestion : suggestions)
			dlg->suggestionLayout->addWidget(dlg->makeAccountRow(suggestion));
	});
}

void DestinationDialog::submitCustom()
{
	if (!termsCheck->isChecked()) {
		showError(QStringLiteral("Error.TermsRequired"));
		return;
	}
	if (serverEdit->text().trimmed().isEmpty() || keyEdit->text().isEmpty()) {
		showError(QStringLiteral("Error.MissingRtmp"));
		return;
	}

	QJsonObject body;
	body.insert(QStringLiteral("type"), QStringLiteral("custom"));
	body.insert(QStringLiteral("platform"), QStringLiteral("custom"));
	const QString preset = presetCombo->currentData().toString();
	if (!preset.isEmpty())
		body.insert(QStringLiteral("custom_platform"), preset);
	body.insert(QStringLiteral("rtmp_url"), serverEdit->text().trimmed());
	body.insert(QStringLiteral("rtmp_key"), keyEdit->text());
	body.insert(QStringLiteral("canvas"), canvasCombo->currentData().toString());
	if (!labelEdit->text().trimmed().isEmpty())
		body.insert(QStringLiteral("label"), labelEdit->text().trimmed());
	body.insert(QStringLiteral("terms_ack"), true);

	customAddButton->setEnabled(false);
	QPointer<DestinationDialog> self(this);
	const DsrRtmpTarget target = {serverEdit->text().trimmed(), keyEdit->text()};
	destinations->create(body, [self, target](bool ok, QString errorKey, QString id) {
		if (!self)
			return;
		self->customAddButton->setEnabled(true);
		if (!ok) {
			self->showError(errorKey);
			return;
		}
		/* Keep a copy so the edit dialog can show what is set. The relay
		 * stores these and never gives them back. */
		dsrSecretStore(id, target);
		self->accept();
	});
}
