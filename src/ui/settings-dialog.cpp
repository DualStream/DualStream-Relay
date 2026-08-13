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

#include "settings-dialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#include <obs-module.h>
#include <obs.h>
#include <plugin-support.h>

#include "../relay-output.h"
#include "../vertical-canvas.hpp"
#include "dsr-ui-common.hpp"
#include "vertical-common.hpp"

namespace {

QLabel *makeNote(const QString &text)
{
	QLabel *note = new QLabel(text);
	note->setObjectName(QStringLiteral("settingsNote"));
	note->setWordWrap(true);
	return note;
}

QPushButton *makeSecondary(const QString &text)
{
	QPushButton *button = new QPushButton(text);
	button->setObjectName(QStringLiteral("secondaryButton"));
	button->setCursor(Qt::PointingHandCursor);
	return button;
}

/* Label on the left, value on the right, for a list of facts rather than a
 * paragraph of them. */
void addFactRow(QVBoxLayout *body, const char *labelKey, const QString &value)
{
	QHBoxLayout *row = new QHBoxLayout;
	row->setContentsMargins(0, 0, 0, 0);

	QLabel *name = new QLabel(dsrText(labelKey));
	name->setObjectName(QStringLiteral("settingsNote"));
	row->addWidget(name);
	row->addStretch();

	QLabel *shown = new QLabel(value);
	shown->setObjectName(QStringLiteral("settingsValue"));
	shown->setTextInteractionFlags(Qt::TextSelectableByMouse);
	row->addWidget(shown);

	body->addLayout(row);
}

} // namespace

SettingsDialog::SettingsDialog(RelayAuth *auth, RelayStatus *status, QWidget *parent)
	: QDialog(parent),
	  auth(auth),
	  status(status)
{
	setWindowTitle(dsrText("Settings.Title"));
	setMinimumWidth(440);
	setStyleSheet(dsrSharedStyleSheet());

	QVBoxLayout *root = new QVBoxLayout(this);
	root->setSpacing(10);

	buildAccountCard(root);
	buildStreamingCard(root);
	buildProtectionCard(root);
	if (VerticalCanvas::instance())
		buildVerticalCard(root);
	buildDiagnosticsCard(root);
	root->addStretch();

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	buttons->button(QDialogButtonBox::Close)->setText(dsrText("Button.Close"));
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	root->addWidget(buttons);

	refreshTarget();
	loadProtection();
}

/* One rounded surface per topic, so the dialog reads as a handful of cards
 * rather than one column of text divided by rules. Returns the layout the
 * card's contents go in. */
QVBoxLayout *SettingsDialog::addCard(QVBoxLayout *root, const char *headerKey)
{
	QFrame *card = new QFrame;
	card->setObjectName(QStringLiteral("settingsCard"));

	QVBoxLayout *body = new QVBoxLayout(card);
	body->setContentsMargins(14, 12, 14, 12);
	body->setSpacing(8);
	body->addWidget(dsrMakeSectionHeader(headerKey));

	root->addWidget(card);
	return body;
}

void SettingsDialog::buildAccountCard(QVBoxLayout *root)
{
	QVBoxLayout *body = addCard(root, "Settings.Account");

	QHBoxLayout *row = new QHBoxLayout;
	row->setContentsMargins(0, 0, 0, 0);
	QLabel *email = new QLabel(!auth->signedIn()         ? dsrText("Settings.NotSignedIn")
				   : auth->email().isEmpty() ? dsrText("Settings.SignedIn")
							     : auth->email());
	email->setTextInteractionFlags(Qt::TextSelectableByMouse);
	row->addWidget(email, 1);

	QPushButton *manage = makeSecondary(dsrText("Settings.OpenAccount"));
	connect(manage, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl(this->auth->webUrl(QStringLiteral("/account")))); });
	row->addWidget(manage);

	QPushButton *signOut = makeSecondary(dsrText("Settings.SignOut"));
	signOut->setVisible(auth->signedIn());
	connect(signOut, &QPushButton::clicked, this, [this]() {
		this->auth->signOut();
		emit signedOut();
		close();
	});
	row->addWidget(signOut);
	body->addLayout(row);

	body->addWidget(makeNote(dsrText("Settings.ConsoleNote")));
}

void SettingsDialog::buildStreamingCard(QVBoxLayout *root)
{
	QVBoxLayout *body = addCard(root, "Settings.Streaming");

	routePill = new QLabel;
	routePill->setObjectName(QStringLiteral("statusPill"));
	QHBoxLayout *pillRow = new QHBoxLayout;
	pillRow->setContentsMargins(0, 0, 0, 0);
	pillRow->addWidget(routePill);
	pillRow->addStretch();
	body->addLayout(pillRow);

	targetLabel = new QLabel;
	targetLabel->setObjectName(QStringLiteral("monoValue"));
	targetLabel->setWordWrap(true);
	targetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	body->addWidget(targetLabel);

	routeButton = new QPushButton(dsrText("Action.UseRelay"));
	routeButton->setObjectName(QStringLiteral("primaryButton"));
	routeButton->setCursor(Qt::PointingHandCursor);
	connect(routeButton, &QPushButton::clicked, this, [this]() {
		emit routeRequested();
		refreshTarget();
	});

	restoreButton = makeSecondary(dsrText("Settings.RestorePrevious"));
	connect(restoreButton, &QPushButton::clicked, this, [this]() {
		emit restoreRequested();
		refreshTarget();
	});

	QHBoxLayout *actions = new QHBoxLayout;
	actions->setContentsMargins(0, 0, 0, 0);
	actions->addWidget(routeButton);
	actions->addWidget(restoreButton);
	actions->addStretch();
	body->addLayout(actions);

	routeNote = makeNote(QString());
	body->addWidget(routeNote);
}

void SettingsDialog::buildProtectionCard(QVBoxLayout *root)
{
	QVBoxLayout *body = addCard(root, "Settings.Protection");

	protectionCheck = new QCheckBox(dsrText("Settings.ProtectionToggle"));
	protectionCheck->setEnabled(false);
	connect(protectionCheck, &QCheckBox::toggled, this, [this](bool on) {
		if (!protectionLoaded)
			return;
		QJsonObject settings;
		settings.insert(QStringLiteral("disconnect_protection"), on);
		this->auth->patch(QStringLiteral("/api/relay/settings"), settings, nullptr);
	});
	body->addWidget(protectionCheck);

	/* Says why the box cannot be moved yet, rather than leaving a greyed
	 * control with no explanation. */
	protectionNote = makeNote(dsrText("Settings.ProtectionLoading"));
	body->addWidget(protectionNote);
}

void SettingsDialog::buildVerticalCard(QVBoxLayout *root)
{
	QVBoxLayout *body = addCard(root, "Settings.Vertical");
	body->addWidget(buildVerticalToggle());
	body->addWidget(makeNote(dsrText("Settings.VerticalNote")));
}

void SettingsDialog::buildDiagnosticsCard(QVBoxLayout *root)
{
	QVBoxLayout *body = addCard(root, "Settings.Diagnostics");

	addFactRow(body, "Settings.Version", QLatin1String(PLUGIN_VERSION));
	const QDateTime lastPoll = status->lastSuccessAt();
	addFactRow(body, "Settings.LastUpdate",
		   lastPoll.isValid() ? lastPoll.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
				      : dsrText("Settings.Never"));

	QPushButton *copyButton = makeSecondary(dsrText("Settings.CopyDiagnostics"));
	connect(copyButton, &QPushButton::clicked, this,
		[this]() { QApplication::clipboard()->setText(buildDiagnostics()); });
	QHBoxLayout *copyRow = new QHBoxLayout;
	copyRow->setContentsMargins(0, 0, 0, 0);
	copyRow->addWidget(copyButton);
	copyRow->addStretch();
	body->addLayout(copyRow);
}

/* The one switch for the portrait canvas. It lives here rather than on the
 * vertical dock because turning it off throws the portrait layouts away and
 * would end a portrait stream in progress; closing a dock must not do either. */
QCheckBox *SettingsDialog::buildVerticalToggle()
{
	QCheckBox *check = new QCheckBox(dsrText("Settings.VerticalToggle"));
	VerticalCanvas *vertical = VerticalCanvas::instance();
	check->setChecked(vertical->enabled());
	check->setEnabled(vertical->ready());

	connect(check, &QCheckBox::toggled, this, [check](bool on) {
		if (dsrSetVerticalEnabled(check->window(), on))
			return;
		/* Backed out of the confirmation, so the box goes back without
		 * running this again. */
		const QSignalBlocker blocker(check);
		check->setChecked(!on);
	});

	return check;
}

/* Where the stream output points, and the one action worth offering for it.
 * Routing is offered only when the output is not already on the relay, and
 * restoring only when there are previous settings to restore, so neither
 * button is ever present with nothing to do. */
void SettingsDialog::refreshTarget()
{
	const bool routed = dsr_route_is_relay();
	const bool haveSnapshot = dsr_route_have_snapshot();

	routePill->setText(dsrText(routed ? "Settings.RouteOn" : "Settings.RouteOff"));
	routePill->setProperty("state", routed ? QStringLiteral("ready") : QStringLiteral("warn"));
	dsrRepolish(routePill);

	char *server = dsr_route_current_server();
	targetLabel->setText(server ? QString::fromUtf8(server) : dsrText("Settings.NoTarget"));
	bfree(server);

	routeButton->setVisible(!routed);
	restoreButton->setVisible(routed && haveSnapshot);
	routeNote->setText(dsrText(routed ? (haveSnapshot ? "Settings.RouteOnNote" : "Settings.RouteNoSnapshot")
					  : "Settings.RouteOffNote"));
}

void SettingsDialog::loadProtection()
{
	QPointer<SettingsDialog> self(this);
	auth->get(QStringLiteral("/api/relay/settings"), [self](const DsrApiResult &result) {
		if (!self)
			return;
		if (!result.ok()) {
			self->protectionNote->setText(dsrText("Settings.ProtectionUnavailable"));
			return;
		}

		const QJsonObject settings = result.body.value(QStringLiteral("settings")).toObject();
		self->protectionLoaded = false;
		self->protectionCheck->setChecked(settings.value(QStringLiteral("disconnect_protection")).toBool(true));
		self->protectionLoaded = true;
		self->protectionCheck->setEnabled(true);
		self->protectionNote->setText(dsrText("Settings.ProtectionNote"));
	});
}

QString SettingsDialog::buildDiagnostics() const
{
	/* Support dump. Deliberately excludes the ingest key and every token;
	 * the server string alone is enough to reason about routing. */
	QJsonObject dump;
	dump.insert(QStringLiteral("plugin_version"), QLatin1String(PLUGIN_VERSION));
	dump.insert(QStringLiteral("obs_version"), QLatin1String(obs_get_version_string()));
	dump.insert(QStringLiteral("signed_in"), auth->signedIn());
	dump.insert(QStringLiteral("routed_to_relay"), dsr_route_is_relay());

	char *server = dsr_route_current_server();
	dump.insert(QStringLiteral("stream_server"), server ? QString::fromUtf8(server) : QString());
	bfree(server);

	dump.insert(QStringLiteral("session_status"), status->sessionStatus());
	dump.insert(QStringLiteral("session_endpoint_available"), status->endpointAvailable());
	dump.insert(QStringLiteral("api_reachable"), status->reachable());
	const QDateTime lastPoll = status->lastSuccessAt();
	dump.insert(QStringLiteral("last_update"), lastPoll.isValid() ? lastPoll.toString(Qt::ISODate) : QString());

	struct dsr_local_stats stats;
	dsr_get_local_stats(&stats);
	dump.insert(QStringLiteral("output_active"), stats.active);
	dump.insert(QStringLiteral("dropped_frames"), stats.dropped_frames);
	dump.insert(QStringLiteral("total_frames"), stats.total_frames);

	return QString::fromUtf8(QJsonDocument(dump).toJson(QJsonDocument::Indented));
}
