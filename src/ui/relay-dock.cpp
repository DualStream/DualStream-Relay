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

#include "relay-dock.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

#include "../relay-output.h"
#include "destination-dialog.hpp"
#include "settings-dialog.hpp"

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

QString platformDisplay(const DsrDestination &dest)
{
	if (!dest.label.isEmpty())
		return dest.label;
	if (dest.platform == QLatin1String("twitch"))
		return QStringLiteral("Twitch");
	if (dest.platform == QLatin1String("youtube"))
		return QStringLiteral("YouTube");
	if (dest.platform == QLatin1String("kick"))
		return QStringLiteral("Kick");
	return dsrText("Destinations.Preset.Generic");
}

QString platformMarkText(const DsrDestination &dest)
{
	const QString source = dest.type == QLatin1String("custom") && !dest.customPlatform.isEmpty()
				       ? dest.customPlatform
				       : dest.platform;
	if (source == QLatin1String("twitch"))
		return QStringLiteral("TW");
	if (source == QLatin1String("youtube"))
		return QStringLiteral("YT");
	if (source == QLatin1String("kick"))
		return QStringLiteral("KK");
	if (source == QLatin1String("tiktok"))
		return QStringLiteral("TT");
	if (source == QLatin1String("facebook") || source == QLatin1String("facebook_reels"))
		return QStringLiteral("FB");
	return QStringLiteral("RT");
}

const char *platformMarkClass(const DsrDestination &dest)
{
	if (dest.platform == QLatin1String("twitch"))
		return "twitch";
	if (dest.platform == QLatin1String("youtube"))
		return "youtube";
	if (dest.platform == QLatin1String("kick"))
		return "kick";
	return "custom";
}

void repolish(QWidget *widget)
{
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

QString settingsFilePath()
{
	char *path = obs_module_config_path("settings.json");
	if (!path)
		return QString();
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

bool readFirstRunDone()
{
	QFile file(settingsFilePath());
	if (!file.open(QIODevice::ReadOnly))
		return false;
	const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
	return obj.value(QStringLiteral("first_run_done")).toBool();
}

void writeFirstRunDone()
{
	char *dir = obs_module_config_path("");
	if (dir) {
		os_mkdirs(dir);
		bfree(dir);
	}
	QJsonObject obj;
	obj.insert(QStringLiteral("first_run_done"), true);
	QSaveFile file(settingsFilePath());
	if (!file.open(QIODevice::WriteOnly))
		return;
	file.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
	file.commit();
}

/* Brand accents from the DualStream design language, applied only where the
 * presentation rules allow: status colors, the primary action and platform
 * marks. Backgrounds and body text inherit the active OBS theme, so the
 * dock looks native in all of them. */
QString dockStyleSheet()
{
	return QStringLiteral(R"(
QLabel#statusPill {
	border-radius: 9px;
	padding: 2px 10px;
	font-weight: 600;
	color: palette(text);
	background: rgba(128, 128, 128, 41);
	border: 1px solid rgba(128, 128, 128, 76);
}
QLabel#statusPill[state="live"] {
	color: #F75B1E;
	background: rgba(247, 91, 30, 36);
	border: 1px solid rgba(247, 91, 30, 89);
}
QLabel#statusPill[state="ready"] {
	color: #088EBC;
	background: rgba(8, 142, 188, 33);
	border: 1px solid rgba(8, 142, 188, 89);
}
QLabel#statusPill[state="protected"] {
	color: #088EBC;
	background: rgba(8, 142, 188, 33);
	border: 1px solid rgba(8, 142, 188, 89);
}
QLabel#statusPill[state="warn"] {
	color: #D97706;
	background: rgba(245, 158, 11, 33);
	border: 1px solid rgba(245, 158, 11, 89);
}
QLabel#statusPill[state="error"] {
	color: #EF4444;
	background: rgba(239, 68, 68, 31);
	border: 1px solid rgba(239, 68, 68, 89);
}
QLabel#banner {
	border-radius: 6px;
	padding: 6px 8px;
}
QLabel#banner[kind="protect"] {
	color: #088EBC;
	background: rgba(8, 142, 188, 31);
}
QLabel#banner[kind="warn"] {
	color: #D97706;
	background: rgba(245, 158, 11, 31);
}
QLabel#banner[kind="error"] {
	color: #EF4444;
	background: rgba(239, 68, 68, 26);
}
QPushButton#primaryButton {
	background-color: #F3490E;
	color: #FFFFFF;
	border: none;
	border-radius: 6px;
	padding: 6px 14px;
	font-weight: 600;
}
QPushButton#primaryButton:hover { background-color: #D63F0C; }
QPushButton#primaryButton:pressed { background-color: #BC3708; }
QPushButton#primaryButton:disabled { background-color: rgba(243, 73, 14, 89); }
QPushButton#endButton {
	background: transparent;
	color: #EF4444;
	border: 1px solid rgba(239, 68, 68, 140);
	border-radius: 6px;
	padding: 4px 12px;
}
QPushButton#endButton:hover { background: rgba(239, 68, 68, 31); }
QPushButton#endButton:pressed { background: rgba(239, 68, 68, 51); }
QLabel#destState[state="live"] { color: #22c55e; font-weight: 600; }
QLabel#destState[state="connecting"] { color: #D97706; }
QLabel#destState[state="reconnecting"] { color: #D97706; }
QLabel#destState[state="rejected"] { color: #EF4444; font-weight: 600; }
QLabel#destState[state="ended"] { color: #8a8a8a; }
QLabel#destError { color: #EF4444; }
QLabel#canvasBadge { color: #8a8a8a; }
QLabel#mutedText { color: #8a8a8a; }
QLabel#platformMark {
	border-radius: 4px;
	padding: 1px 4px;
	font-weight: 700;
	font-size: 10px;
	color: palette(text);
	background: rgba(128, 128, 128, 64);
}
QLabel#platformMark[platform="twitch"] { background: #9146FF; color: #FFFFFF; }
QLabel#platformMark[platform="youtube"] { background: #FF0000; color: #FFFFFF; }
QLabel#platformMark[platform="kick"] { background: #53FC18; color: #111111; }
)");
}

} // namespace

RelayDock::RelayDock(QWidget *parent) : QWidget(parent)
{
	auth = new RelayAuth(this);
	destinations = new RelayDestinations(auth, this);
	status = new RelayStatus(auth, this);

	QVBoxLayout *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(6);

	/* header */
	QHBoxLayout *header = new QHBoxLayout;
	statusPill = new QLabel;
	statusPill->setObjectName(QStringLiteral("statusPill"));
	header->addWidget(statusPill);

	timerLabel = new QLabel;
	timerLabel->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	timerLabel->setVisible(false);
	header->addWidget(timerLabel);

	header->addStretch();

	gearButton = new QToolButton;
	gearButton->setText(QStringLiteral("..."));
	gearButton->setToolTip(dsrText("Dock.Settings"));
	gearButton->setAutoRaise(true);
	connect(gearButton, &QToolButton::clicked, this, &RelayDock::openSettings);
	header->addWidget(gearButton);
	root->addLayout(header);

	/* banner */
	banner = new QLabel;
	banner->setObjectName(QStringLiteral("banner"));
	banner->setWordWrap(true);
	banner->setVisible(false);
	root->addWidget(banner);

	bannerAction = new QPushButton;
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
	messageLayout->setContentsMargins(0, 8, 0, 0);
	messageLabel = new QLabel;
	messageLabel->setWordWrap(true);
	messageLayout->addWidget(messageLabel);

	codeRow = new QWidget;
	QHBoxLayout *codeLayout = new QHBoxLayout(codeRow);
	codeLayout->setContentsMargins(0, 4, 0, 0);
	codeLabel = new QLabel;
	codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	QFont codeFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	codeFont.setPointSize(codeFont.pointSize() + 6);
	codeFont.setBold(true);
	codeLabel->setFont(codeFont);
	codeLayout->addWidget(codeLabel);
	QPushButton *copyCode = new QPushButton(dsrText("Button.Copy"));
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
	QHBoxLayout *primaryRow = new QHBoxLayout;
	primaryRow->addWidget(primaryButton);
	primaryRow->addStretch();
	messageLayout->addLayout(primaryRow);
	messageLayout->addStretch();
	stack->addWidget(messagePage);

	listPage = new QWidget;
	QVBoxLayout *listPageLayout = new QVBoxLayout(listPage);
	listPageLayout->setContentsMargins(0, 0, 0, 0);
	preflightLabel = new QLabel;
	preflightLabel->setWordWrap(true);
	preflightLabel->setObjectName(QStringLiteral("mutedText"));
	preflightLabel->setVisible(false);
	listPageLayout->addWidget(preflightLabel);

	scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	listContainer = new QWidget;
	listLayout = new QVBoxLayout(listContainer);
	listLayout->setContentsMargins(0, 0, 0, 0);
	listLayout->setSpacing(2);
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
	addButton = new QPushButton(dsrText("Footer.AddDestination"));
	addButton->setFlat(true);
	connect(addButton, &QPushButton::clicked, this, &RelayDock::openAddDialog);
	footerLayout->addWidget(addButton);
	statsLabel = new QLabel;
	statsLabel->setObjectName(QStringLiteral("mutedText"));
	statsLabel->setVisible(false);
	footerLayout->addWidget(statsLabel);
	footerLayout->addStretch();
	countLabel = new QLabel;
	countLabel->setObjectName(QStringLiteral("mutedText"));
	footerLayout->addWidget(countLabel);
	endButton = new QPushButton;
	endButton->setObjectName(QStringLiteral("endButton"));
	endButton->setVisible(false);
	connect(endButton, &QPushButton::clicked, this, [this]() { status->requestEnd(); });
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

	tick = new QTimer(this);
	tick->setInterval(1000);
	connect(tick, &QTimer::timeout, this, &RelayDock::refreshTick);
	tick->start();

	setStyleSheet(dockStyleSheet());
	setMinimumWidth(240);
	refreshUi();
}

RelayDock::State RelayDock::computeState() const
{
	if (!auth->signedIn())
		return auth->pairing() ? State::Pairing : State::SignedOut;
	if (lapsed)
		return State::Lapsed;
	if (status->ending())
		return State::Ending;
	if (destOffline || !status->reachable())
		return State::Offline;
	if (status->sessionStatus() == QLatin1String("protected"))
		return State::Protected;

	const bool streaming = obs_frontend_streaming_active();
	if (streaming || status->hasSession())
		return State::Live;
	if (!destinations->loaded())
		return State::Checking;
	if (destinations->list().isEmpty())
		return State::Unconfigured;
	if (!dsr_route_is_relay())
		return State::NotRouted;
	return State::Ready;
}

void RelayDock::setPill(State state)
{
	const char *property = "neutral";
	const char *word = "State.Checking";

	switch (state) {
	case State::Checking:
		word = "State.Checking";
		break;
	case State::SignedOut:
		word = "State.SignedOut";
		break;
	case State::Pairing:
		word = "State.Pairing";
		break;
	case State::Lapsed:
		word = "State.Lapsed";
		property = "error";
		break;
	case State::Unconfigured:
		word = "State.Unconfigured";
		break;
	case State::NotRouted:
		word = "State.NotRouted";
		property = "warn";
		break;
	case State::Ready:
		word = "State.Ready";
		property = "ready";
		break;
	case State::Live:
		word = "State.Live";
		property = "live";
		break;
	case State::Protected:
		word = "State.Protected";
		property = "protected";
		break;
	case State::Ending:
		word = "State.Ending";
		break;
	case State::Offline:
		word = "State.Offline";
		property = "warn";
		break;
	}

	statusPill->setText(dsrText(word));
	statusPill->setProperty("state", QLatin1String(property));
	repolish(statusPill);
}

void RelayDock::setBanner(const QString &text, const char *kind, const QString &actionText,
			  std::function<void()> action)
{
	if (text.isEmpty()) {
		banner->setVisible(false);
		bannerAction->setVisible(false);
		bannerActionFn = nullptr;
		return;
	}

	banner->setText(text);
	banner->setProperty("kind", QLatin1String(kind));
	repolish(banner);
	banner->setVisible(true);

	if (actionText.isEmpty()) {
		bannerAction->setVisible(false);
		bannerActionFn = nullptr;
	} else {
		bannerAction->setText(actionText);
		bannerActionFn = std::move(action);
		bannerAction->setVisible(true);
	}
}

bool RelayDock::keyMismatch() const
{
	if (!targetFetched || !dsr_route_is_relay())
		return false;
	char *key = dsr_route_current_key();
	const bool mismatch = key && targetKey != QString::fromUtf8(key);
	bfree(key);
	return mismatch;
}

QString RelayDock::elapsedText() const
{
	qint64 seconds = 0;
	const QDateTime started = status->sessionStartedAt();
	if (started.isValid())
		seconds = started.secsTo(QDateTime::currentDateTimeUtc());
	else if (localStreamStartMs > 0)
		seconds = (QDateTime::currentMSecsSinceEpoch() - localStreamStartMs) / 1000;
	if (seconds < 0)
		seconds = 0;

	return QStringLiteral("%1:%2:%3")
		.arg(seconds / 3600)
		.arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
		.arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString RelayDock::protectedBannerText() const
{
	QDateTime since = status->protectedSince();
	if (!since.isValid())
		since = protectedLocalSince;

	qint64 remaining = destinations->graceWindowSeconds();
	if (since.isValid())
		remaining -= since.secsTo(QDateTime::currentDateTimeUtc());
	if (remaining < 0)
		remaining = 0;

	const QString countdown =
		QStringLiteral("%1:%2").arg(remaining / 60).arg(remaining % 60, 2, 10, QLatin1Char('0'));

	if (!obs_frontend_streaming_active())
		return QString(dsrText("Protected.Resume")).arg(countdown);
	return QString(dsrText("Protected.Banner")).arg(countdown);
}

QString RelayDock::preflightText() const
{
	QStringList lines;

	QStringList landscape;
	QStringList portrait;
	for (const DsrDestination &dest : destinations->list()) {
		if (!dest.enabled)
			continue;
		const QString name = platformDisplay(dest);
		if (dest.canvas != QLatin1String("portrait"))
			landscape.append(name);
		if (dest.canvas == QLatin1String("portrait") || dest.canvas == QLatin1String("both"))
			portrait.append(name);
	}
	if (!landscape.isEmpty())
		lines.append(QString(dsrText("Preflight.Landscape")).arg(landscape.join(QStringLiteral(", "))));
	if (!portrait.isEmpty())
		lines.append(QString(dsrText("Preflight.Portrait")).arg(portrait.join(QStringLiteral(", "))));

	lines.append(
		dsrText(destinations->hasEnabledTwitch() ? "Preflight.BitrateTwitch" : "Preflight.BitrateStandard"));

	struct dsr_video_summary video;
	if (dsr_get_video_summary(&video)) {
		const uint32_t shortSide = video.output_width < video.output_height ? video.output_width
										    : video.output_height;
		if (shortSide > 1080)
			lines.append(dsrText("Preflight.Downscale"));
		if (video.fps > 60.5)
			lines.append(dsrText("Preflight.FpsCap"));
	}

	const int bitrate = dsr_get_configured_bitrate_kbps();
	if (bitrate > 8000)
		lines.append(dsrText("Preflight.BitrateHigh"));

	lines.append(dsrText("Preflight.StartHint"));
	return lines.join(QStringLiteral("\n"));
}

QString RelayDock::summaryText(State state) const
{
	if (state == State::Live || state == State::Protected) {
		int live = 0;
		int issues = 0;
		for (const DsrDestStatus &dest : status->destinations()) {
			if (dest.state == QLatin1String("live"))
				live++;
			else if (dest.state == QLatin1String("rejected"))
				issues++;
		}
		return QString(dsrText("Summary.Live")).arg(live).arg(issues);
	}
	return statusPill->text();
}

void RelayDock::rebuildRows()
{
	/* Drop everything but the trailing stretch, then rebuild. The list is
	 * at most eight rows, so rebuilding beats bookkeeping. */
	while (listLayout->count() > 1) {
		QLayoutItem *item = listLayout->takeAt(0);
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}

	const QVector<DsrDestStatus> &liveStates = status->destinations();
	for (const DsrDestination &dest : destinations->list()) {
		const DsrDestStatus *live = nullptr;
		for (const DsrDestStatus &state : liveStates) {
			if (state.destinationId == dest.id) {
				live = &state;
				break;
			}
		}
		listLayout->insertWidget(listLayout->count() - 1, makeRow(dest, live));
	}
}

QWidget *RelayDock::makeRow(const DsrDestination &dest, const DsrDestStatus *live)
{
	QWidget *row = new QWidget;
	QVBoxLayout *outer = new QVBoxLayout(row);
	outer->setContentsMargins(0, 2, 0, 2);
	outer->setSpacing(1);

	QWidget *line = new QWidget;
	QHBoxLayout *lineLayout = new QHBoxLayout(line);
	lineLayout->setContentsMargins(0, 0, 0, 0);
	lineLayout->setSpacing(6);

	QLabel *mark = new QLabel(platformMarkText(dest));
	mark->setObjectName(QStringLiteral("platformMark"));
	mark->setProperty("platform", QLatin1String(platformMarkClass(dest)));
	lineLayout->addWidget(mark);

	QLabel *name = new QLabel(platformDisplay(dest));
	lineLayout->addWidget(name, 1);

	QLabel *canvas = new QLabel(canvasDisplay(dest.canvas));
	canvas->setObjectName(QStringLiteral("canvasBadge"));
	lineLayout->addWidget(canvas);

	if (live && !live->state.isEmpty()) {
		QLabel *state = new QLabel;
		state->setObjectName(QStringLiteral("destState"));
		state->setProperty("state", live->state);
		const QByteArray stateKey = "DestState." + live->state.toUtf8();
		state->setText(dsrText(stateKey.constData()));
		lineLayout->addWidget(state);
	}

	QCheckBox *toggle = new QCheckBox;
	toggle->setChecked(dest.enabled);
	toggle->setToolTip(dsrText("Destinations.EnabledTip"));
	const QString id = dest.id;
	connect(toggle, &QCheckBox::clicked, this, [this, id](bool checked) {
		QJsonObject body;
		body.insert(QStringLiteral("enabled"), checked);
		destinations->modify(id, body, nullptr);
	});
	lineLayout->addWidget(toggle);

	QToolButton *menuButton = new QToolButton;
	menuButton->setText(QStringLiteral("..."));
	menuButton->setAutoRaise(true);
	menuButton->setPopupMode(QToolButton::InstantPopup);
	QMenu *menu = new QMenu(menuButton);

	const DsrDestination destCopy = dest;
	menu->addAction(dsrText("Destinations.Edit"), this, [this, destCopy]() {
		DestinationDialog dialog(auth, destinations, destCopy, this);
		dialog.exec();
	});

	if (dest.type == QLatin1String("custom")) {
		menu->addAction(dsrText("Destinations.Test"), this, [this, id]() {
			destinations->test(id, [this](bool ok, bool reachable) {
				QMessageBox::information(this, dsrText("Destinations.TestTitle"),
							 dsrText(!ok ? "Destinations.TestFailed"
								     : (reachable ? "Destinations.TestReachable"
										  : "Destinations.TestUnreachable")));
			});
		});
	}

	menu->addAction(dsrText("Destinations.Remove"), this, [this, id]() {
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this, dsrText("Destinations.RemoveTitle"), dsrText("Destinations.RemoveConfirm"));
		if (answer == QMessageBox::Yes)
			destinations->remove(id, nullptr);
	});

	menuButton->setMenu(menu);
	lineLayout->addWidget(menuButton);
	outer->addWidget(line);

	/* The relay's own error string is the most useful thing on screen
	 * when a destination is refused; show it whole and selectable. */
	if (live && live->state == QLatin1String("rejected") && !live->lastError.isEmpty()) {
		QLabel *error = new QLabel(live->lastError);
		error->setObjectName(QStringLiteral("destError"));
		error->setWordWrap(true);
		error->setTextInteractionFlags(Qt::TextSelectableByMouse);
		outer->addWidget(error);
	}

	return row;
}

void RelayDock::refreshUi()
{
	const State state = computeState();

	if (state == State::Protected && current != State::Protected && !status->protectedSince().isValid())
		protectedLocalSince = QDateTime::currentDateTimeUtc();
	if (state != State::Protected && current == State::Protected)
		protectedLocalSince = QDateTime();

	current = state;
	setPill(state);

	const bool showTimer = state == State::Live || state == State::Protected || state == State::Ending;
	timerLabel->setVisible(showTimer);
	if (showTimer)
		timerLabel->setText(elapsedText());

	/* banner, one at a time, highest priority first */
	switch (state) {
	case State::Protected:
		setBanner(protectedBannerText(), "protect", QString(), nullptr);
		break;
	case State::Offline: {
		const QDateTime last = status->lastSuccessAt();
		const QString when = last.isValid() ? last.toLocalTime().toString(QStringLiteral("hh:mm:ss"))
						    : dsrText("Settings.Never");
		setBanner(QString(dsrText("Offline.Banner")).arg(when), "warn", dsrText("Button.Retry"),
			  [this]() { refreshAll(); });
		break;
	}
	case State::SignedOut:
		if (dsr_route_is_relay())
			setBanner(dsrText("Warning.SignedOutRouted"), "warn", QString(), nullptr);
		else
			setBanner(QString(), "warn", QString(), nullptr);
		break;
	case State::Ready:
	case State::Live:
		if (destinations->loaded() && destinations->enabledCount() == 0) {
			setBanner(dsrText("Warning.NoDestinations"), "error", QString(), nullptr);
		} else if (state == State::Ready && keyMismatch()) {
			setBanner(dsrText("Warning.KeyRotated"), "warn", dsrText("Button.Update"),
				  [this]() { routeToRelay(); });
		} else {
			setBanner(QString(), "warn", QString(), nullptr);
		}
		break;
	default:
		setBanner(QString(), "warn", QString(), nullptr);
		break;
	}

	/* content */
	const bool listState = state == State::Ready || state == State::Live || state == State::Protected ||
			       state == State::Offline;
	if (listState) {
		stack->setCurrentWidget(listPage);
		preflightLabel->setVisible(state == State::Ready);
		if (state == State::Ready)
			preflightLabel->setText(preflightText());
		rebuildRows();
		/* Offline shows the last known rows, visibly inert. */
		listContainer->setEnabled(state != State::Offline);
	} else {
		stack->setCurrentWidget(messagePage);
		codeRow->setVisible(false);
		urlLabel->setVisible(false);
		primaryButton->setVisible(true);

		switch (state) {
		case State::Checking:
			messageLabel->setText(dsrText("State.CheckingBody"));
			primaryButton->setVisible(false);
			break;
		case State::SignedOut:
			messageLabel->setText(pairingError.isEmpty() ? dsrText("SignedOut.Body")
								     : dsrText("SignedOut.Body") +
									       QStringLiteral("\n\n") + pairingError);
			primaryButton->setText(dsrText("Action.SignIn"));
			break;
		case State::Pairing:
			messageLabel->setText(dsrText("Pairing.Body"));
			codeLabel->setText(auth->pairingCode());
			codeRow->setVisible(true);
			urlLabel->setText(auth->pairingUrl());
			urlLabel->setVisible(true);
			primaryButton->setText(dsrText("Button.Cancel"));
			break;
		case State::Lapsed:
			messageLabel->setText(dsrText("Lapsed.Body"));
			primaryButton->setText(dsrText("Action.OpenBilling"));
			break;
		case State::Unconfigured:
			messageLabel->setText(dsrText("Unconfigured.Body"));
			primaryButton->setText(dsrText("Footer.AddDestination"));
			break;
		case State::NotRouted: {
			char *server = dsr_route_current_server();
			const QString target = server ? QString::fromUtf8(server) : dsrText("Settings.NoTarget");
			bfree(server);
			messageLabel->setText(QString(dsrText("NotRouted.Body")).arg(target));
			primaryButton->setText(dsrText("Action.UseRelay"));
			break;
		}
		case State::Ending:
			messageLabel->setText(dsrText("Ending.Body"));
			primaryButton->setVisible(false);
			break;
		default:
			break;
		}
	}

	/* footer */
	const bool liveish = state == State::Live || state == State::Protected || state == State::Ending;
	const bool configState = state == State::Ready || state == State::Unconfigured || state == State::NotRouted;
	addButton->setVisible(configState && destinations->loaded());
	addButton->setEnabled(destinations->list().size() < destinations->maxDestinations());
	countLabel->setVisible(configState && destinations->loaded());
	countLabel->setText(
		QString(dsrText("Footer.Count")).arg(destinations->list().size()).arg(destinations->maxDestinations()));
	statsLabel->setVisible(liveish && state != State::Ending);
	endButton->setVisible(liveish && state != State::Ending);
	endButton->setText(dsrText(state == State::Protected ? "Action.EndStreamNow" : "Action.EndStream"));
	footer->setVisible(!compact && (configState || liveish));

	/* compact mode keeps only the header and one summary line */
	stack->setVisible(!compact);
	summaryLabel->setVisible(compact);
	if (compact)
		summaryLabel->setText(summaryText(state));
}

void RelayDock::refreshTick()
{
	if (current == State::Live || current == State::Protected || current == State::Ending) {
		timerLabel->setText(elapsedText());
		if (current == State::Protected)
			banner->setText(protectedBannerText());

		struct dsr_local_stats stats;
		dsr_get_local_stats(&stats);
		if (stats.active && stats.total_frames > 0) {
			const double percent = 100.0 * stats.dropped_frames / stats.total_frames;
			statsLabel->setText(QString(dsrText("Footer.Uplink"))
						    .arg(stats.dropped_frames)
						    .arg(QString::number(percent, 'f', 1)));
		} else {
			statsLabel->setText(QString());
		}

		auth->ensureFreshToken();
	}
}

void RelayDock::refreshAll()
{
	destOffline = false;
	destinations->refresh();
	status->pollNow();
	if (auth->signedIn() && !targetFetched)
		fetchIngestTarget(nullptr);
	refreshUi();
}

void RelayDock::fetchIngestTarget(std::function<void(bool)> done)
{
	if (targetFetchInFlight) {
		if (done)
			done(false);
		return;
	}
	targetFetchInFlight = true;

	auth->get(QStringLiteral("/api/relay/ingest-target"), [this, done](const DsrApiResult &result) {
		targetFetchInFlight = false;
		if (!result.ok()) {
			if (done)
				done(false);
			return;
		}

		const QJsonObject target = result.body.value(QStringLiteral("target")).toObject();
		const QJsonObject rtmps = target.value(QStringLiteral("rtmps")).toObject();
		targetServer = rtmps.value(QStringLiteral("server")).toString();
		targetKey = rtmps.value(QStringLiteral("landscape")).toObject().value(QStringLiteral("key")).toString();
		targetFetched = !targetServer.isEmpty() && !targetKey.isEmpty();
		refreshUi();
		if (done)
			done(targetFetched);
	});
}

void RelayDock::routeToRelay()
{
	if (targetFetched) {
		dsr_route_apply(targetServer.toUtf8().constData(), targetKey.toUtf8().constData());
		refreshUi();
		return;
	}
	fetchIngestTarget([this](bool ok) {
		if (ok) {
			dsr_route_apply(targetServer.toUtf8().constData(), targetKey.toUtf8().constData());
			refreshUi();
		}
	});
}

void RelayDock::restoreRoute()
{
	dsr_route_restore();
	refreshUi();
}

void RelayDock::openSettings()
{
	SettingsDialog dialog(auth, status, this);
	connect(&dialog, &SettingsDialog::routeRequested, this, &RelayDock::routeToRelay);
	connect(&dialog, &SettingsDialog::restoreRequested, this, &RelayDock::restoreRoute);
	dialog.exec();
	refreshUi();
}

void RelayDock::openAddDialog()
{
	DestinationDialog dialog(auth, destinations, this);
	dialog.exec();
	destinations->refresh();
}

void RelayDock::endStreamHotkey()
{
	if (current == State::Live || current == State::Protected)
		status->requestEnd();
}

void RelayDock::firstRunShow()
{
	if (firstRunHandled)
		return;
	firstRunHandled = true;

	if (readFirstRunDone())
		return;
	writeFirstRunDone();
	showDockWindow();
}

void RelayDock::showDockWindow()
{
	/* obs_frontend_add_dock_by_id wraps this widget in a QDockWidget that
	 * starts hidden. Reaching the wrapper through the parent is the only
	 * handle a plugin gets; fail quietly if the cast does not hold. */
	QDockWidget *dock = qobject_cast<QDockWidget *>(parentWidget());
	if (dock) {
		dock->setVisible(true);
		dock->raise();
	}
}

QSize RelayDock::sizeHint() const
{
	return QSize(320, 480);
}

void RelayDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	const bool nowCompact = height() > 0 && height() < 200;
	if (nowCompact != compact) {
		compact = nowCompact;
		refreshUi();
	}
}

void RelayDock::handleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		firstRunShow();
		refreshAll();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		auth->ensureFreshToken();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		localStreamStartMs = QDateTime::currentMSecsSinceEpoch();
		status->setLivePolling(true);
		status->pollNow();
		refreshUi();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		/* The single most important call in the plugin: without it the
		 * relay reads a stop as a connection drop and holds every
		 * platform on the standby slate for the grace window. */
		if (auth->signedIn())
			status->requestEnd();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		localStreamStartMs = 0;
		status->setLivePolling(false);
		/* Backstop for a missed STOPPING event, and the poll that
		 * clears the Ending state. */
		if (auth->signedIn() && status->hasSession() && !status->ending())
			status->requestEnd();
		QTimer::singleShot(3000, this, [this]() { status->pollNow(); });
		refreshUi();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		refreshUi();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		if (auth->signedIn() && obs_frontend_streaming_active() && !status->ending())
			status->requestEnd();
		if (status->ending()) {
			/* Give the end call a bounded window to reach the
			 * server before the process goes away. */
			QEventLoop loop;
			QTimer::singleShot(1500, &loop, &QEventLoop::quit);
			connect(status, &RelayStatus::endPosted, &loop, &QEventLoop::quit);
			loop.exec();
		}
		break;
	default:
		break;
	}
}

/* ---- module glue -------------------------------------------------------- */

static RelayDock *dockInstance = nullptr;
static obs_hotkey_id endHotkeyId = OBS_INVALID_HOTKEY_ID;

static void frontend_event_cb(enum obs_frontend_event event, void *)
{
	if (dockInstance)
		dockInstance->handleFrontendEvent(event);
}

static void tools_menu_cb(void *)
{
	if (dockInstance)
		dockInstance->showDockWindow();
}

static void end_hotkey_cb(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed && dockInstance)
		QMetaObject::invokeMethod(dockInstance, "endStreamHotkey", Qt::QueuedConnection);
}

extern "C" bool dsr_frontend_init(void)
{
	dockInstance = new RelayDock();
	if (!obs_frontend_add_dock_by_id("dsr_relay_dock", obs_module_text("Dock.Title"), dockInstance)) {
		delete dockInstance;
		dockInstance = nullptr;
		return false;
	}

	obs_frontend_add_event_callback(frontend_event_cb, nullptr);
	obs_frontend_add_tools_menu_item(obs_module_text("Menu.ShowDock"), tools_menu_cb, nullptr);
	endHotkeyId = obs_hotkey_register_frontend("dsr_end_stream", obs_module_text("Hotkey.EndStream"), end_hotkey_cb,
						   nullptr);
	return true;
}

extern "C" void dsr_frontend_shutdown(void)
{
	if (endHotkeyId != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(endHotkeyId);
		endHotkeyId = OBS_INVALID_HOTKEY_ID;
	}
	obs_frontend_remove_event_callback(frontend_event_cb, nullptr);
	/* The dock widget itself belongs to the OBS main window and is
	 * destroyed with it. */
	dockInstance = nullptr;
}
