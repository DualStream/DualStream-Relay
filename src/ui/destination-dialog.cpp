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

#include "destination-dialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <obs-module.h>

#include <algorithm>

namespace {

QString dsrText(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString canvasDisplay(const QString &canvas)
{
	if (canvas == QLatin1String("portrait"))
		return dsrText("Canvas.Portrait");
	if (canvas == QLatin1String("both"))
		return dsrText("Canvas.Both");
	return dsrText("Canvas.Landscape");
}

QFrame *makeSeparator()
{
	QFrame *line = new QFrame;
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	return line;
}

} // namespace

QStringList DestinationDialog::allowedCanvases(const QString &platform)
{
	/* The relay fans out Twitch over RTMP, which cannot carry a portrait
	 * program (that needs WHIP enhanced broadcasting), and Kick has no
	 * portrait ingest. The server accepts these combinations anyway, so
	 * the gate lives here. */
	if (platform == QLatin1String("twitch") || platform == QLatin1String("kick"))
		return {QStringLiteral("landscape")};
	return {QStringLiteral("landscape"), QStringLiteral("portrait"), QStringLiteral("both")};
}

void DestinationDialog::fillCanvasCombo(QComboBox *combo, const QString &platform, const QString &selected,
					QLabel *note)
{
	const QStringList allowed = allowedCanvases(platform);
	combo->clear();
	for (const QString &canvas : allowed)
		combo->addItem(canvasDisplay(canvas), canvas);

	int index = combo->findData(selected);
	combo->setCurrentIndex(index >= 0 ? index : 0);
	combo->setEnabled(allowed.size() > 1);

	if (note) {
		const bool locked = allowed.size() == 1;
		note->setVisible(locked);
		if (locked)
			note->setText(dsrText("Destinations.LandscapeOnly"));
	}
}

DestinationDialog::DestinationDialog(RelayAuth *auth, RelayDestinations *destinations, QWidget *parent)
	: QDialog(parent),
	  auth(auth),
	  destinations(destinations)
{
	setWindowTitle(dsrText("Destinations.AddTitle"));
	buildAddUi();
	loadSuggestions();
}

DestinationDialog::DestinationDialog(RelayAuth *auth, RelayDestinations *destinations, const DsrDestination &existing,
				     QWidget *parent)
	: QDialog(parent),
	  auth(auth),
	  destinations(destinations),
	  editMode(true),
	  existing(existing)
{
	setWindowTitle(dsrText("Destinations.EditTitle"));
	buildEditUi();
}

void DestinationDialog::buildAddUi()
{
	setMinimumWidth(420);
	QVBoxLayout *layout = new QVBoxLayout(this);

	QLabel *accountsHeader = new QLabel(dsrText("Destinations.ConnectedAccounts"));
	accountsHeader->setStyleSheet(QStringLiteral("font-weight: 600;"));
	layout->addWidget(accountsHeader);

	QWidget *suggestionHost = new QWidget;
	suggestionLayout = new QVBoxLayout(suggestionHost);
	suggestionLayout->setContentsMargins(0, 0, 0, 0);
	suggestionEmptyLabel = new QLabel(dsrText("Destinations.LoadingAccounts"));
	suggestionEmptyLabel->setWordWrap(true);
	suggestionLayout->addWidget(suggestionEmptyLabel);
	layout->addWidget(suggestionHost);

	layout->addWidget(makeSeparator());

	QLabel *customHeader = new QLabel(dsrText("Destinations.CustomRtmp"));
	customHeader->setStyleSheet(QStringLiteral("font-weight: 600;"));
	layout->addWidget(customHeader);

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

	connect(presetCombo, &QComboBox::currentIndexChanged, this, [this]() {
		const QString preset = presetCombo->currentData().toString();
		const QString suggested =
			(preset == QLatin1String("tiktok") || preset == QLatin1String("facebook_reels"))
				? QStringLiteral("portrait")
				: canvasCombo->currentData().toString();
		fillCanvasCombo(canvasCombo, QString(), suggested, canvasNote);
	});

	customAddButton = new QPushButton(dsrText("Destinations.Add"));
	customAddButton->setObjectName(QStringLiteral("primaryButton"));
	connect(customAddButton, &QPushButton::clicked, this, &DestinationDialog::submitCustom);
	QHBoxLayout *customButtonRow = new QHBoxLayout;
	customButtonRow->addStretch();
	customButtonRow->addWidget(customAddButton);
	layout->addLayout(customButtonRow);

	layout->addWidget(makeSeparator());

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

		suggestions.erase(std::remove_if(suggestions.begin(), suggestions.end(),
						 [](const DsrSuggestion &s) { return s.alreadyAdded; }),
				  suggestions.end());

		if (suggestions.isEmpty()) {
			dlg->suggestionEmptyLabel->setText(dsrText("Destinations.NoAccounts"));
			return;
		}

		dlg->suggestionEmptyLabel->setVisible(false);
		for (const DsrSuggestion &suggestion : suggestions) {
			QWidget *row = new QWidget(dlg);
			QHBoxLayout *rowLayout = new QHBoxLayout(row);
			rowLayout->setContentsMargins(0, 2, 0, 2);

			QLabel *name = new QLabel(
				suggestion.label.isEmpty()
					? QStringLiteral("%1 - %2").arg(suggestion.platform, suggestion.username)
					: suggestion.label);
			rowLayout->addWidget(name, 1);

			QComboBox *canvas = new QComboBox;
			fillCanvasCombo(canvas, suggestion.platform,
					suggestion.suggestedCanvas.isEmpty() ? QStringLiteral("landscape")
									     : suggestion.suggestedCanvas,
					nullptr);
			rowLayout->addWidget(canvas);

			QPushButton *add = new QPushButton(dsrText("Destinations.Add"));
			rowLayout->addWidget(add);

			const QString platform = suggestion.platform;
			const QString accountId = suggestion.accountId;
			QObject::connect(add, &QPushButton::clicked, dlg, [self, platform, accountId, canvas, add]() {
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
				self->destinations->create(body, [self, addGuard](bool ok, QString errorKey) {
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

			dlg->suggestionLayout->addWidget(row);
		}
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
	destinations->create(body, [self](bool ok, QString errorKey) {
		if (!self)
			return;
		self->customAddButton->setEnabled(true);
		if (ok)
			self->accept();
		else
			self->showError(errorKey);
	});
}

void DestinationDialog::buildEditUi()
{
	setMinimumWidth(420);
	QVBoxLayout *layout = new QVBoxLayout(this);
	QFormLayout *form = new QFormLayout;

	labelEdit = new QLineEdit(existing.label);
	form->addRow(dsrText("Destinations.Label"), labelEdit);

	canvasCombo = new QComboBox;
	canvasNote = new QLabel;
	canvasNote->setWordWrap(true);
	fillCanvasCombo(canvasCombo, existing.platform, existing.canvas, canvasNote);
	form->addRow(dsrText("Destinations.Canvas"), canvasCombo);

	if (existing.type == QLatin1String("custom")) {
		serverEdit = new QLineEdit;
		serverEdit->setPlaceholderText(dsrText("Destinations.Unchanged"));
		form->addRow(dsrText("Destinations.Server"), serverEdit);

		/* The API never returns a stored key, so the field cannot be
		 * prefilled. Both fields together replace the stored pair. */
		keyEdit = new QLineEdit;
		keyEdit->setEchoMode(QLineEdit::Password);
		keyEdit->setPlaceholderText(dsrText("Destinations.Unchanged"));
		form->addRow(dsrText("Destinations.StreamKey"), keyEdit);
	}

	layout->addLayout(form);
	layout->addWidget(canvasNote);

	if (existing.platform == QLatin1String("youtube")) {
		layout->addWidget(makeSeparator());
		QLabel *ytHeader = new QLabel(dsrText("Destinations.YouTubeMeta"));
		ytHeader->setStyleSheet(QStringLiteral("font-weight: 600;"));
		layout->addWidget(ytHeader);

		const QJsonObject meta = existing.metadata.value(QStringLiteral("youtube")).toObject();
		QFormLayout *ytForm = new QFormLayout;
		ytLandscapeTitle = new QLineEdit(meta.value(QStringLiteral("landscape_title")).toString());
		ytForm->addRow(dsrText("Destinations.YouTubeLandscapeTitle"), ytLandscapeTitle);
		ytPortraitTitle = new QLineEdit(meta.value(QStringLiteral("portrait_title")).toString());
		ytForm->addRow(dsrText("Destinations.YouTubePortraitTitle"), ytPortraitTitle);
		ytDescription = new QPlainTextEdit(meta.value(QStringLiteral("description")).toString());
		ytDescription->setMaximumHeight(72);
		ytForm->addRow(dsrText("Destinations.YouTubeDescription"), ytDescription);
		ytPrivacy = new QComboBox;
		ytPrivacy->addItem(dsrText("Destinations.Privacy.Public"), QStringLiteral("public"));
		ytPrivacy->addItem(dsrText("Destinations.Privacy.Unlisted"), QStringLiteral("unlisted"));
		ytPrivacy->addItem(dsrText("Destinations.Privacy.Private"), QStringLiteral("private"));
		const int index = ytPrivacy->findData(meta.value(QStringLiteral("privacy_status")).toString());
		if (index >= 0)
			ytPrivacy->setCurrentIndex(index);
		ytForm->addRow(dsrText("Destinations.YouTubePrivacy"), ytPrivacy);
		layout->addLayout(ytForm);
	}

	errorLabel = new QLabel;
	errorLabel->setWordWrap(true);
	errorLabel->setStyleSheet(QStringLiteral("color: #EF4444;"));
	errorLabel->setVisible(false);
	layout->addWidget(errorLabel);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Save)->setText(dsrText("Button.Save"));
	buttons->button(QDialogButtonBox::Cancel)->setText(dsrText("Button.Cancel"));
	connect(buttons, &QDialogButtonBox::accepted, this, &DestinationDialog::submitEdit);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

void DestinationDialog::submitEdit()
{
	QJsonObject body;
	if (labelEdit->text().trimmed() != existing.label)
		body.insert(QStringLiteral("label"), labelEdit->text().trimmed());
	const QString canvas = canvasCombo->currentData().toString();
	if (canvas != existing.canvas)
		body.insert(QStringLiteral("canvas"), canvas);

	if (serverEdit && keyEdit) {
		const QString server = serverEdit->text().trimmed();
		const QString key = keyEdit->text();
		if (!server.isEmpty() || !key.isEmpty()) {
			if (server.isEmpty() || key.isEmpty()) {
				showError(QStringLiteral("Error.MissingRtmp"));
				return;
			}
			body.insert(QStringLiteral("rtmp_url"), server);
			body.insert(QStringLiteral("rtmp_key"), key);
		}
	}

	if (ytLandscapeTitle) {
		QJsonObject youtube;
		if (!ytLandscapeTitle->text().trimmed().isEmpty())
			youtube.insert(QStringLiteral("landscape_title"), ytLandscapeTitle->text().trimmed());
		if (!ytPortraitTitle->text().trimmed().isEmpty())
			youtube.insert(QStringLiteral("portrait_title"), ytPortraitTitle->text().trimmed());
		if (!ytDescription->toPlainText().trimmed().isEmpty())
			youtube.insert(QStringLiteral("description"), ytDescription->toPlainText().trimmed());
		youtube.insert(QStringLiteral("privacy_status"), ytPrivacy->currentData().toString());

		QJsonObject metadata;
		metadata.insert(QStringLiteral("youtube"), youtube);
		body.insert(QStringLiteral("metadata"), metadata);
	}

	if (body.isEmpty()) {
		accept();
		return;
	}

	QPointer<DestinationDialog> self(this);
	destinations->modify(existing.id, body, [self](bool ok, QString errorKey) {
		if (!self)
			return;
		if (ok)
			self->accept();
		else
			self->showError(errorKey);
	});
}

void DestinationDialog::showError(const QString &errorKey)
{
	errorLabel->setText(dsrText(errorKey.toUtf8().constData()));
	errorLabel->setVisible(true);
}
