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

namespace {

QString dsrText(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QFrame *makeSeparator()
{
	QFrame *line = new QFrame;
	line->setFrameShape(QFrame::HLine);
	line->setFrameShadow(QFrame::Sunken);
	return line;
}

QLabel *makeHeader(const char *key)
{
	QLabel *label = new QLabel(dsrText(key));
	label->setStyleSheet(QStringLiteral("font-weight: 600;"));
	return label;
}

} // namespace

SettingsDialog::SettingsDialog(RelayAuth *auth, RelayStatus *status, QWidget *parent)
	: QDialog(parent),
	  auth(auth),
	  status(status)
{
	setWindowTitle(dsrText("Settings.Title"));
	setMinimumWidth(420);

	QVBoxLayout *layout = new QVBoxLayout(this);

	layout->addWidget(makeHeader("Settings.Account"));
	QHBoxLayout *accountRow = new QHBoxLayout;
	QLabel *emailLabel = new QLabel(auth->email().isEmpty() ? dsrText("Settings.SignedIn") : auth->email());
	accountRow->addWidget(emailLabel, 1);
	QPushButton *signOutButton = new QPushButton(dsrText("Settings.SignOut"));
	connect(signOutButton, &QPushButton::clicked, this, [this]() {
		this->auth->signOut();
		emit signedOut();
		close();
	});
	accountRow->addWidget(signOutButton);
	layout->addLayout(accountRow);

	layout->addWidget(makeSeparator());

	layout->addWidget(makeHeader("Settings.Protection"));
	protectionCheck = new QCheckBox(dsrText("Settings.ProtectionToggle"));
	protectionCheck->setEnabled(false);
	connect(protectionCheck, &QCheckBox::toggled, this, [this](bool on) {
		if (!protectionLoaded)
			return;
		QJsonObject body;
		body.insert(QStringLiteral("disconnect_protection"), on);
		this->auth->patch(QStringLiteral("/api/relay/settings"), body, nullptr);
	});
	layout->addWidget(protectionCheck);
	QLabel *protectionNote = new QLabel(dsrText("Settings.ProtectionNote"));
	protectionNote->setWordWrap(true);
	protectionNote->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
	layout->addWidget(protectionNote);

	layout->addWidget(makeSeparator());

	layout->addWidget(makeHeader("Settings.Streaming"));
	targetLabel = new QLabel;
	targetLabel->setWordWrap(true);
	targetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(targetLabel);

	QHBoxLayout *routeRow = new QHBoxLayout;
	QPushButton *routeButton = new QPushButton(dsrText("Action.UseRelay"));
	routeButton->setObjectName(QStringLiteral("primaryButton"));
	connect(routeButton, &QPushButton::clicked, this, [this]() {
		emit routeRequested();
		refreshTarget();
	});
	routeRow->addWidget(routeButton);

	QPushButton *restoreButton = new QPushButton(dsrText("Settings.RestorePrevious"));
	restoreButton->setEnabled(dsr_route_have_snapshot());
	connect(restoreButton, &QPushButton::clicked, this, [this, restoreButton]() {
		emit restoreRequested();
		restoreButton->setEnabled(dsr_route_have_snapshot());
		refreshTarget();
	});
	routeRow->addWidget(restoreButton);
	routeRow->addStretch();
	layout->addLayout(routeRow);

	layout->addWidget(makeSeparator());

	QHBoxLayout *linksRow = new QHBoxLayout;
	QPushButton *consoleLink = new QPushButton(dsrText("Settings.OpenConsole"));
	consoleLink->setFlat(true);
	connect(consoleLink, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl(this->auth->webUrl(QStringLiteral("/relay")))); });
	linksRow->addWidget(consoleLink);

	QPushButton *billingLink = new QPushButton(dsrText("Settings.OpenBilling"));
	billingLink->setFlat(true);
	connect(billingLink, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl(this->auth->webUrl(QStringLiteral("/dashboard")))); });
	linksRow->addWidget(billingLink);
	linksRow->addStretch();
	layout->addLayout(linksRow);

	QLabel *linksNote = new QLabel(dsrText("Settings.ConsoleNote"));
	linksNote->setWordWrap(true);
	linksNote->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
	layout->addWidget(linksNote);

	layout->addWidget(makeSeparator());

	layout->addWidget(makeHeader("Settings.Diagnostics"));
	QLabel *versionLabel =
		new QLabel(QStringLiteral("%1 %2").arg(dsrText("Settings.Version"), QLatin1String(PLUGIN_VERSION)));
	layout->addWidget(versionLabel);
	const QDateTime lastPoll = status->lastSuccessAt();
	QLabel *pollLabel = new QLabel(QStringLiteral("%1 %2").arg(dsrText("Settings.LastUpdate"),
								   lastPoll.isValid() ? lastPoll.toString(Qt::ISODate)
										      : dsrText("Settings.Never")));
	layout->addWidget(pollLabel);

	QPushButton *copyButton = new QPushButton(dsrText("Settings.CopyDiagnostics"));
	connect(copyButton, &QPushButton::clicked, this,
		[this]() { QApplication::clipboard()->setText(buildDiagnostics()); });
	QHBoxLayout *copyRow = new QHBoxLayout;
	copyRow->addWidget(copyButton);
	copyRow->addStretch();
	layout->addLayout(copyRow);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	buttons->button(QDialogButtonBox::Close)->setText(dsrText("Button.Close"));
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);

	refreshTarget();
	loadProtection();
}

void SettingsDialog::refreshTarget()
{
	char *server = dsr_route_current_server();
	if (server) {
		targetLabel->setText(
			QStringLiteral("%1 %2").arg(dsrText("Settings.CurrentTarget"), QString::fromUtf8(server)));
		bfree(server);
	} else {
		targetLabel->setText(dsrText("Settings.NoTarget"));
	}
}

void SettingsDialog::loadProtection()
{
	QPointer<SettingsDialog> self(this);
	auth->get(QStringLiteral("/api/relay/settings"), [self](const DsrApiResult &result) {
		if (!self || !result.ok())
			return;
		const QJsonObject settings = result.body.value(QStringLiteral("settings")).toObject();
		self->protectionLoaded = false;
		self->protectionCheck->setChecked(settings.value(QStringLiteral("disconnect_protection")).toBool(true));
		self->protectionLoaded = true;
		self->protectionCheck->setEnabled(true);
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
