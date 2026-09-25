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

/* The dialog shell: canvas rules shared by both modes, the constructors, and
 * everything the edit mode needs. Add mode lives in destination-dialog-add.cpp. */

#include "destination-dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <obs-frontend-api.h>

#include "../ladder.h"
#include "../relay-output.h"
#include "../relay-secrets.hpp"
#include "dsr-ui-common.hpp"

QStringList DestinationDialog::allowedCanvases(const QString &platform)
{
	/* Twitch takes no portrait stream of its own: "both" there is dual
	 * format, one Enhanced Broadcasting session carrying the landscape and
	 * portrait programs together, which the relay serves from a ladder
	 * this plugin encodes. A build without that output must not offer it,
	 * or the relay would wait for renditions that never arrive. Kick has
	 * no portrait ingest at all. The server accepts every combination, so
	 * the gate lives here. */
	if (platform == QLatin1String("twitch")) {
		if (dsr_ladder_available())
			return {QStringLiteral("landscape"), QStringLiteral("both")};
		return {QStringLiteral("landscape")};
	}
	if (platform == QLatin1String("kick"))
		return {QStringLiteral("landscape")};
	return {QStringLiteral("landscape"), QStringLiteral("portrait"), QStringLiteral("both")};
}

void DestinationDialog::fillCanvasCombo(QComboBox *combo, const QString &platform, const QString &selected,
					QLabel *note)
{
	const QStringList allowed = allowedCanvases(platform);
	const bool twitch = platform == QLatin1String("twitch");
	combo->clear();
	for (const QString &canvas : allowed) {
		const bool dualFormat = twitch && canvas == QLatin1String("both");
		/* The same words the rows show as a badge, capitalised here
		 * the way a list of choices reads. */
		QString label = dualFormat ? dsrText("Canvas.DualFormat") : dsrCanvasDisplay(canvas);
		if (!label.isEmpty())
			label[0] = label[0].toUpper();
		combo->addItem(label, canvas);
	}

	int index = combo->findData(selected);
	combo->setCurrentIndex(index >= 0 ? index : 0);
	combo->setEnabled(allowed.size() > 1);

	if (note) {
		const bool locked = allowed.size() == 1;
		note->setVisible(locked || twitch);
		if (locked)
			note->setText(dsrText("Destinations.LandscapeOnly"));
		else if (twitch)
			note->setText(dsrText("Destinations.TwitchBoth"));
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
		/* The API never returns a stored server or key, so the only
		 * values that can be shown are the copies kept on this machine.
		 * Without them both fields stay blank, and filling them in
		 * replaces the stored pair. */
		cached = dsrSecretLoad(existing.id);

		serverEdit = new QLineEdit(cached.url);
		serverEdit->setPlaceholderText(dsrText("Destinations.Unchanged"));
		form->addRow(dsrText("Destinations.Server"), serverEdit);

		keyEdit = new QLineEdit(cached.key);
		keyEdit->setEchoMode(QLineEdit::Password);
		keyEdit->setPlaceholderText(dsrText("Destinations.Unchanged"));
		QPushButton *reveal = new QPushButton(dsrText("Destinations.RevealKey"));
		reveal->setObjectName(QStringLiteral("secondaryButton"));
		reveal->setCheckable(true);
		reveal->setCursor(Qt::PointingHandCursor);
		connect(reveal, &QPushButton::toggled, this,
			[this](bool on) { keyEdit->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password); });
		QHBoxLayout *keyRow = new QHBoxLayout;
		keyRow->setContentsMargins(0, 0, 0, 0);
		keyRow->addWidget(keyEdit, 1);
		keyRow->addWidget(reveal);
		form->addRow(dsrText("Destinations.StreamKey"), keyRow);
	}

	layout->addLayout(form);
	layout->addWidget(canvasNote);

	if (existing.platform == QLatin1String("youtube")) {
		layout->addWidget(dsrMakeSeparator());
		layout->addWidget(dsrMakeSectionHeader("Destinations.YouTubeMeta"));
		QLabel *scheduled = new QLabel(dsrText("Destinations.YouTubeScheduled"));
		scheduled->setObjectName(QStringLiteral("mutedText"));
		scheduled->setWordWrap(true);
		layout->addWidget(scheduled);

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
		ytPrivacy->addItem(dsrText("Destinations.Privacy.Inherit"), QString());
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

	DsrRtmpTarget target;
	if (serverEdit && keyEdit) {
		target.url = serverEdit->text().trimmed();
		target.key = keyEdit->text();

		/* The relay replaces the pair, so either one changing sends both
		 * and both have to be there. Untouched prefilled fields are not
		 * a change and are left out of the patch entirely. */
		const bool changed = target.url != cached.url || target.key != cached.key;
		if (changed && !target.isEmpty()) {
			if (target.url.isEmpty() || target.key.isEmpty()) {
				showError(QStringLiteral("Error.MissingRtmp"));
				return;
			}
			body.insert(QStringLiteral("rtmp_url"), target.url);
			body.insert(QStringLiteral("rtmp_key"), target.key);
		} else {
			target = DsrRtmpTarget();
		}
	}

	if (ytLandscapeTitle) {
		/* Only fields changed here are sent; an emptied field is sent as
		 * null, which clears it on the relay. Fields this dialog does not
		 * show stay as stored. */
		const QJsonObject before = existing.metadata.value(QStringLiteral("youtube")).toObject();
		QJsonObject youtube;
		const auto put = [&youtube, &before](const char *key, const QString &value) {
			const QLatin1String name(key);
			if (value == before.value(name).toString())
				return;
			youtube.insert(name, value.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(value));
		};
		put("landscape_title", ytLandscapeTitle->text().trimmed());
		put("portrait_title", ytPortraitTitle->text().trimmed());
		put("description", ytDescription->toPlainText().trimmed());
		put("privacy_status", ytPrivacy->currentData().toString());

		if (!youtube.isEmpty()) {
			QJsonObject patch;
			patch.insert(QStringLiteral("youtube"), youtube);
			body.insert(QStringLiteral("metadata_patch"), patch);
		}
	}

	if (body.isEmpty()) {
		accept();
		return;
	}
	if (canvas != existing.canvas && !confirmCanvasChangeWhileLive(canvas))
		return;

	QPointer<DestinationDialog> self(this);
	const QString id = existing.id;
	destinations->modify(id, body, [self, id, target](bool ok, QString errorKey) {
		if (!self)
			return;
		if (!ok) {
			self->showError(errorKey);
			return;
		}
		if (!target.isEmpty())
			dsrSecretStore(id, target);
		self->accept();
	});
}

/* What a canvas change does to a destination that is live is not obvious and
 * not symmetric: a picture taken away ends its broadcast for this stream,
 * Twitch keeps the shape its session started with until the next go-live, and
 * a picture added needs no warning. */
bool DestinationDialog::confirmCanvasChangeWhileLive(const QString &canvas)
{
	if (!existing.enabled || !obs_frontend_streaming_active() || !dsr_route_is_relay())
		return true;

	const auto carries = [](const QString &value, const char *which) {
		return value == QLatin1String("both") || value == QLatin1String(which);
	};
	const bool dropsDesktop = carries(existing.canvas, "landscape") && !carries(canvas, "landscape");
	const bool dropsMobile = carries(existing.canvas, "portrait") && !carries(canvas, "portrait");
	const char *key = existing.platform == QLatin1String("twitch") ? "Destinations.CanvasLiveTwitch"
			  : dropsMobile                                ? "Destinations.CanvasLiveEndsMobile"
			  : dropsDesktop                               ? "Destinations.CanvasLiveEndsDesktop"
								       : nullptr;
	if (!key)
		return true;

	QMessageBox box(this);
	box.setWindowTitle(dsrText("Destinations.CanvasLiveTitle"));
	box.setText(dsrText(key));
	QPushButton *confirm = box.addButton(dsrText("Button.Save"), QMessageBox::AcceptRole);
	box.addButton(dsrText("Button.Cancel"), QMessageBox::RejectRole);
	box.setDefaultButton(confirm);
	box.exec();
	return box.clickedButton() == confirm;
}

void DestinationDialog::showError(const QString &errorKey)
{
	errorLabel->setText(dsrText(errorKey.toUtf8().constData()));
	errorLabel->setVisible(true);
}
