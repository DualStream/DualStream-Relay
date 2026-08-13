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

/* Construction of the relay dock's widget tree. */

#include "relay-dock.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "dsr-ui-common.hpp"
#include "dsr-widgets.hpp"
#include "relay-dock-text.hpp"

RelayDock::RelayDock(QWidget *parent) : QWidget(parent)
{
	auth = new RelayAuth(this);
	destinations = new RelayDestinations(auth, this);
	status = new RelayStatus(auth, this);

	QVBoxLayout *root = new QVBoxLayout(this);
	root->setContentsMargins(12, 12, 12, 12);
	root->setSpacing(10);

	/* header */
	QHBoxLayout *header = new QHBoxLayout;
	header->setSpacing(8);
	statusPill = new QLabel;
	statusPill->setObjectName(QStringLiteral("statusPill"));
	header->addWidget(statusPill);

	timerLabel = new QLabel;
	timerLabel->setObjectName(QStringLiteral("timerLabel"));
	timerLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	timerLabel->setVisible(false);
	header->addWidget(timerLabel);

	header->addStretch();

	gearButton = new DsrIconButton(DsrIconButton::Glyph::Settings);
	gearButton->setToolTip(dsrText("Dock.Settings"));
	connect(gearButton, &QAbstractButton::clicked, this, &RelayDock::openSettings);
	header->addWidget(gearButton);
	root->addLayout(header);

	/* banner */
	banner = new QLabel;
	banner->setObjectName(QStringLiteral("banner"));
	banner->setWordWrap(true);
	banner->setVisible(false);
	root->addWidget(banner);

	bannerAction = new QPushButton;
	bannerAction->setObjectName(QStringLiteral("secondaryButton"));
	bannerAction->setCursor(Qt::PointingHandCursor);
	bannerAction->setVisible(false);
	connect(bannerAction, &QPushButton::clicked, this, [this]() {
		if (bannerActionFn)
			bannerActionFn();
	});
	root->addWidget(bannerAction);

	/* content */
	stack = new QStackedWidget;

	messagePage = new QWidget;
	QVBoxLayout *messageLayout = new QVBoxLayout(messagePage);
	messageLayout->setContentsMargins(0, 4, 0, 0);
	messageLayout->setSpacing(12);
	messageLabel = new QLabel;
	messageLabel->setObjectName(QStringLiteral("bodyText"));
	messageLabel->setWordWrap(true);
	messageLayout->addWidget(messageLabel);

	codeRow = new QWidget;
	QHBoxLayout *codeLayout = new QHBoxLayout(codeRow);
	codeLayout->setContentsMargins(0, 0, 0, 0);
	codeLayout->setSpacing(10);
	codeLabel = new QLabel;
	codeLabel->setObjectName(QStringLiteral("codeLabel"));
	codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	codeFont.setPointSize(codeFont.pointSize() + 7);
	codeFont.setBold(true);
	codeLabel->setFont(codeFont);
	codeLayout->addWidget(codeLabel);
	QPushButton *copyCode = new QPushButton(dsrText("Button.Copy"));
	copyCode->setObjectName(QStringLiteral("secondaryButton"));
	copyCode->setCursor(Qt::PointingHandCursor);
	connect(copyCode, &QPushButton::clicked, this,
		[this]() { QApplication::clipboard()->setText(codeLabel->text()); });
	codeLayout->addWidget(copyCode);
	codeLayout->addStretch();
	messageLayout->addWidget(codeRow);

	urlLabel = new QLabel;
	urlLabel->setObjectName(QStringLiteral("mutedText"));
	urlLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	urlLabel->setWordWrap(true);
	messageLayout->addWidget(urlLabel);

	primaryButton = new QPushButton;
	primaryButton->setObjectName(QStringLiteral("primaryButton"));
	primaryButton->setCursor(Qt::PointingHandCursor);
	QHBoxLayout *primaryRow = new QHBoxLayout;
	primaryRow->addWidget(primaryButton);
	primaryRow->addStretch();
	messageLayout->addLayout(primaryRow);
	messageLayout->addStretch();
	stack->addWidget(messagePage);

	listPage = new QWidget;
	QVBoxLayout *listPageLayout = new QVBoxLayout(listPage);
	listPageLayout->setContentsMargins(0, 0, 0, 0);
	listPageLayout->setSpacing(10);

	/* No preflight card. The destination rows already say which platforms
	 * are enabled and which canvas each takes, so restating it above them
	 * was the same information twice. Anything genuinely wrong still shows
	 * in the banner, which appears only when there is something to say. */
	scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	listContainer = new QWidget;
	listLayout = new QVBoxLayout(listContainer);
	listLayout->setContentsMargins(0, 0, 0, 0);
	listLayout->setSpacing(6);
	listLayout->addStretch();
	scroll->setWidget(listContainer);
	listPageLayout->addWidget(scroll, 1);
	stack->addWidget(listPage);

	root->addWidget(stack, 1);

	summaryLabel = new QLabel;
	summaryLabel->setVisible(false);
	root->addWidget(summaryLabel);

	/* footer */
	footer = new QWidget;
	QHBoxLayout *footerLayout = new QHBoxLayout(footer);
	footerLayout->setContentsMargins(0, 0, 0, 0);
	footerLayout->setSpacing(8);
	/* The plus speaks for itself next to the count it acts on; the words
	 * were the widest thing in the footer and said nothing extra. */
	addButton = new DsrIconButton(DsrIconButton::Glyph::Plus);
	addButton->setToolTip(dsrText("Footer.AddDestination"));
	connect(addButton, &QAbstractButton::clicked, this, &RelayDock::openAddDialog);
	footerLayout->addWidget(addButton);
	footerLayout->addStretch();
	countLabel = new QLabel;
	countLabel->setObjectName(QStringLiteral("mutedText"));
	footerLayout->addWidget(countLabel);
	endButton = new QPushButton;
	endButton->setObjectName(QStringLiteral("endButton"));
	endButton->setCursor(Qt::PointingHandCursor);
	endButton->setVisible(false);
	connect(endButton, &QPushButton::clicked, this, [this]() { endEverything(); });
	footerLayout->addWidget(endButton);
	root->addWidget(footer);

	connect(primaryButton, &QPushButton::clicked, this, [this]() {
		switch (current) {
		case State::SignedOut:
			pairingError.clear();
			auth->startPairing();
			break;
		case State::Pairing:
			auth->cancelPairing();
			break;
		case State::Lapsed:
			QDesktopServices::openUrl(QUrl(auth->webUrl(QStringLiteral("/dashboard"))));
			break;
		case State::Unconfigured:
			openAddDialog();
			break;
		case State::NotRouted:
			routeToRelay();
			break;
		default:
			break;
		}
	});

	connect(auth, &RelayAuth::stateChanged, this, [this]() {
		if (auth->signedIn())
			refreshAll();
		else
			refreshUi();
	});
	connect(auth, &RelayAuth::pairingChanged, this, &RelayDock::refreshUi);
	connect(auth, &RelayAuth::pairingFinished, this, [this](bool ok, const QString &errorKey) {
		if (ok) {
			pairingError.clear();
			refreshAll();
		} else if (!errorKey.isEmpty()) {
			pairingError = dsrText(errorKey.toUtf8().constData());
		}
		refreshUi();
	});

	connect(destinations, &RelayDestinations::changed, this, [this]() {
		lapsed = false;
		destOffline = false;
		refreshUi();
	});
	connect(destinations, &RelayDestinations::loadFailed, this,
		[this](int httpStatus, const QString &code, bool transportOk) {
			if (!transportOk) {
				destOffline = true;
			} else if (httpStatus == 401) {
				/* Token dead even after a refresh attempt. */
				auth->signOut();
			} else if (httpStatus == 403 && (code == QLatin1String("ENTITLEMENT_INACTIVE") ||
							 code == QLatin1String("SUBSCRIPTION_INACTIVE"))) {
				lapsed = true;
			}
			refreshUi();
		});

	connect(status, &RelayStatus::updated, this, &RelayDock::refreshUi);
	connect(status, &RelayStatus::endFinished, this, [this](bool) { refreshUi(); });

	/* Started by showEvent, so a dock the user has closed does no work. */
	tick = new QTimer(this);
	tick->setInterval(1000);
	connect(tick, &QTimer::timeout, this, &RelayDock::refreshTick);

	setStyleSheet(dsrRelayStyleSheet());
	setMinimumWidth(260);
	refreshUi();
}
