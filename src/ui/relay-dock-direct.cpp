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

/* The single destination an account without a subscription can set up: one
 * RTMP server, carrying either the desktop or the mobile program. It is held
 * on this machine and the relay is not in the path, so there is nothing here
 * that reaches out to DualStream.
 *
 * Once saved it collapses to a row like the ones the dock shows for relay
 * destinations, so the page settles into something read rather than something
 * half filled in. The pencil puts the form back. */

#include "relay-dock.hpp"

#include <QComboBox>
#include <QDesktopServices>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "../relay-direct.hpp"
#include "../vertical-canvas.hpp"
#include "dsr-platform-mark.hpp"
#include "dsr-ui-common.hpp"

namespace {

/* The host on its own. The full URL is mostly boilerplate the user pasted and
 * does not need reading back to them. */
QString serverSummary(const QString &url)
{
	const QString host = QUrl(url).host();
	return host.isEmpty() ? url : host;
}

} // namespace

void RelayDock::buildDirectPage()
{
	directPage = new QWidget;
	QVBoxLayout *layout = new QVBoxLayout(directPage);
	layout->setContentsMargins(0, 4, 0, 0);
	layout->setSpacing(10);

	directIntro = new QLabel(dsrText("Direct.Body"));
	directIntro->setObjectName(QStringLiteral("bodyText"));
	directIntro->setWordWrap(true);
	layout->addWidget(directIntro);

	buildDirectSummary(layout);
	buildDirectForm(layout);

	directStatus = new QLabel;
	directStatus->setWordWrap(true);
	directStatus->setVisible(false);
	layout->addWidget(directStatus);

	layout->addWidget(dsrMakeSeparator());

	/* The upsell. One destination and one canvas is the shape of the free
	 * tier, so the thing worth buying is the two together. */
	QLabel *upsell = new QLabel(dsrText("Direct.Upsell"));
	upsell->setObjectName(QStringLiteral("bodyText"));
	upsell->setWordWrap(true);
	layout->addWidget(upsell);

	QPushButton *upgrade = new QPushButton(dsrText("Direct.Upgrade"));
	upgrade->setObjectName(QStringLiteral("primaryButton"));
	upgrade->setCursor(Qt::PointingHandCursor);
	connect(upgrade, &QPushButton::clicked, this,
		[this]() { QDesktopServices::openUrl(QUrl(auth->webUrl(QStringLiteral("/dashboard")))); });
	QHBoxLayout *upgradeRow = new QHBoxLayout;
	upgradeRow->setContentsMargins(0, 0, 0, 0);
	upgradeRow->addWidget(upgrade);
	upgradeRow->addStretch();
	layout->addLayout(upgradeRow);

	layout->addStretch();
	stack->addWidget(directPage);
}

/* What a saved destination looks like: the same row shape the dock uses for
 * relay destinations, so the two read as the same kind of thing. */
void RelayDock::buildDirectSummary(QVBoxLayout *parent)
{
	directSummary = new QWidget;
	directSummary->setObjectName(QStringLiteral("destRow"));
	QHBoxLayout *row = new QHBoxLayout(directSummary);
	row->setContentsMargins(10, 8, 8, 8);
	row->setSpacing(9);

	/* No logo to show: a custom server is whatever the user pasted, so the
	 * neutral broadcast mark is the honest one. */
	QLabel *mark = new QLabel;
	mark->setPixmap(dsrPlatformMark(QString(), 28, devicePixelRatioF()));
	mark->setFixedSize(28, 28);
	row->addWidget(mark);

	QVBoxLayout *lines = new QVBoxLayout;
	lines->setContentsMargins(0, 0, 0, 0);
	lines->setSpacing(1);
	directSummaryName = new QLabel;
	directSummaryName->setObjectName(QStringLiteral("destName"));
	lines->addWidget(directSummaryName);
	directSummaryNote = new QLabel(dsrText("Direct.KeySaved"));
	directSummaryNote->setObjectName(QStringLiteral("settingsNote"));
	lines->addWidget(directSummaryNote);
	row->addLayout(lines, 1);

	directSummaryCanvas = new QLabel;
	directSummaryCanvas->setObjectName(QStringLiteral("canvasBadge"));
	row->addWidget(directSummaryCanvas);

	DsrIconButton *edit = new DsrIconButton(DsrIconButton::Glyph::Pencil);
	edit->setToolTip(dsrText("Direct.Edit"));
	connect(edit, &QAbstractButton::clicked, this, [this]() {
		directEditing = true;
		directStatus->setVisible(false);
		refreshDirectPage();
	});
	row->addWidget(edit);

	parent->addWidget(directSummary);
}

void RelayDock::buildDirectForm(QVBoxLayout *parent)
{
	directForm = new QWidget;
	QVBoxLayout *outer = new QVBoxLayout(directForm);
	outer->setContentsMargins(0, 0, 0, 0);
	outer->setSpacing(10);

	QFormLayout *form = new QFormLayout;
	form->setContentsMargins(0, 0, 0, 0);
	directServerEdit = new QLineEdit;
	directServerEdit->setPlaceholderText(QStringLiteral("rtmp://"));
	form->addRow(dsrText("Destinations.Server"), directServerEdit);

	directKeyEdit = new QLineEdit;
	directKeyEdit->setEchoMode(QLineEdit::Password);
	form->addRow(dsrText("Destinations.StreamKey"), directKeyEdit);

	directCanvasCombo = new QComboBox;
	directCanvasCombo->addItem(dsrText("Direct.Desktop"), QStringLiteral("landscape"));
	directCanvasCombo->addItem(dsrText("Direct.Mobile"), QStringLiteral("portrait"));
	form->addRow(dsrText("Destinations.Canvas"), directCanvasCombo);
	outer->addLayout(form);

	/* Which button drives the stream depends on the canvas, and getting
	 * that wrong is the easiest way to be live on nothing. */
	directHint = new QLabel;
	directHint->setObjectName(QStringLiteral("settingsNote"));
	directHint->setWordWrap(true);
	outer->addWidget(directHint);

	QHBoxLayout *actions = new QHBoxLayout;
	actions->setContentsMargins(0, 0, 0, 0);
	directSaveButton = new QPushButton(dsrText("Direct.Save"));
	directSaveButton->setObjectName(QStringLiteral("primaryButton"));
	directSaveButton->setCursor(Qt::PointingHandCursor);
	connect(directSaveButton, &QPushButton::clicked, this, &RelayDock::saveDirectDestination);
	actions->addWidget(directSaveButton);

	directCancelButton = new QPushButton(dsrText("Button.Cancel"));
	directCancelButton->setObjectName(QStringLiteral("secondaryButton"));
	directCancelButton->setCursor(Qt::PointingHandCursor);
	connect(directCancelButton, &QPushButton::clicked, this, [this]() {
		directEditing = false;
		directStatus->setVisible(false);
		fillDirectForm();
		refreshDirectPage();
	});
	actions->addWidget(directCancelButton);

	directRemoveButton = new QPushButton(dsrText("Destinations.Remove"));
	directRemoveButton->setObjectName(QStringLiteral("endButton"));
	directRemoveButton->setCursor(Qt::PointingHandCursor);
	connect(directRemoveButton, &QPushButton::clicked, this, &RelayDock::removeDirectDestination);
	actions->addWidget(directRemoveButton);
	actions->addStretch();
	outer->addLayout(actions);

	connect(directCanvasCombo, &QComboBox::currentIndexChanged, this, &RelayDock::refreshDirectPage);
	parent->addWidget(directForm);
}

/* Put the stored values back in the fields, discarding anything half typed. */
void RelayDock::fillDirectForm()
{
	const DsrDirectDestination stored = dsrDirectLoad();
	directServerEdit->setText(stored.url);
	directKeyEdit->setText(stored.key);

	const int index = directCanvasCombo->findData(stored.canvas);
	const QSignalBlocker blocker(directCanvasCombo);
	directCanvasCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void RelayDock::refreshDirectPage()
{
	if (!directPage)
		return;

	const DsrDirectDestination stored = dsrDirectLoad();
	const bool have = !stored.isEmpty();

	/* Nothing stored means there is nothing to collapse into, so the form is
	 * the only thing that makes sense to show. */
	if (!have)
		directEditing = true;
	if (have && !directFormFilled) {
		fillDirectForm();
		directFormFilled = true;
	}

	directSummary->setVisible(have && !directEditing);
	directForm->setVisible(directEditing);
	directIntro->setVisible(directEditing);

	if (have) {
		directSummaryName->setText(serverSummary(stored.url));
		directSummaryCanvas->setText(dsrText(stored.isPortrait() ? "Direct.Mobile" : "Direct.Desktop"));
	}

	if (!directEditing)
		return;

	directCancelButton->setVisible(have);
	directRemoveButton->setVisible(have);
	directSaveButton->setText(dsrText(have ? "Button.Save" : "Direct.Save"));

	const bool portrait = directCanvasCombo->currentData().toString() == QLatin1String("portrait");
	directHint->setText(dsrText(portrait ? "Direct.MobileHint" : "Direct.DesktopHint"));
}

/* Says which way it went, and how the stream is started when it saved, so the
 * next step is on screen rather than left to be discovered. */
void RelayDock::showDirectStatus(const QString &text, bool ok)
{
	directStatus->setText(text);
	directStatus->setObjectName(ok ? QStringLiteral("settingsNote") : QStringLiteral("destError"));
	dsrRepolish(directStatus);
	directStatus->setVisible(true);
}

void RelayDock::saveDirectDestination()
{
	DsrDirectDestination destination;
	destination.url = directServerEdit->text().trimmed();
	destination.key = directKeyEdit->text();
	destination.canvas = directCanvasCombo->currentData().toString();

	if (destination.isEmpty()) {
		showDirectStatus(dsrText("Error.MissingRtmp"), false);
		return;
	}

	if (!dsrDirectStore(destination)) {
		showDirectStatus(dsrText("Direct.Unavailable"), false);
		return;
	}

	directEditing = false;
	showDirectStatus(dsrText(destination.isPortrait() ? "Direct.SavedMobile" : "Direct.SavedDesktop"), true);
	applyDirectDestination();
	if (VerticalCanvas::instance())
		VerticalCanvas::instance()->notifyDirectChanged();
	refreshUi();
}

void RelayDock::removeDirectDestination()
{
	const QMessageBox::StandardButton answer =
		QMessageBox::question(this, dsrText("Destinations.RemoveTitle"), dsrText("Direct.RemoveConfirm"));
	if (answer != QMessageBox::Yes)
		return;

	if (VerticalCanvas::instance())
		VerticalCanvas::instance()->stopDirect();

	dsrDirectForget();
	if (VerticalCanvas::instance())
		VerticalCanvas::instance()->notifyDirectChanged();

	directEditing = true;
	directFormFilled = false;
	directServerEdit->clear();
	directKeyEdit->clear();
	showDirectStatus(dsrText("Direct.Removed"), true);
	refreshUi();
}

/* Point whichever program the destination takes at it. The desktop program
 * rides OBS's own stream output, so routing it is all this has to do and OBS's
 * Start Streaming does the rest. The mobile program has no such button and is
 * started from the vertical dock instead. */
void RelayDock::applyDirectDestination()
{
	const DsrDirectDestination stored = dsrDirectLoad();
	if (stored.isEmpty() || stored.isPortrait())
		return;

	dsr_route_apply(stored.url.toUtf8().constData(), stored.key.toUtf8().constData());
}
