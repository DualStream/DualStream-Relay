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

/* The standby screen card: what disconnect protection plays in place of the
 * feed. Sits beside the toggle that decides whether it is ever shown. */

#include "settings-dialog.hpp"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "../relay-slate.hpp"
#include "dsr-ui-common.hpp"

namespace {

/* The relay sorts an upload into one of three kinds, and the card names the
 * one it got: a GIF and a silent video are easy to confuse at the file
 * dialog, and they play differently. */
const char *slateSetKey(const QString &mediaType)
{
	if (mediaType == QLatin1String("image"))
		return "Slate.SetImage";
	if (mediaType == QLatin1String("gif"))
		return "Slate.SetGif";
	if (mediaType == QLatin1String("video"))
		return "Slate.SetVideo";
	return "Slate.Set";
}

} // namespace

void SettingsDialog::buildSlateCard(QVBoxLayout *root)
{
	QVBoxLayout *body = addCard(root, "Settings.Slate");

	slateState = new QLabel;
	slateState->setObjectName(QStringLiteral("settingsValue"));
	slateState->setWordWrap(true);
	body->addWidget(slateState);

	QHBoxLayout *actions = new QHBoxLayout;
	actions->setContentsMargins(0, 0, 0, 0);

	slateChoose = new QPushButton;
	slateChoose->setObjectName(QStringLiteral("secondaryButton"));
	slateChoose->setCursor(Qt::PointingHandCursor);
	connect(slateChoose, &QPushButton::clicked, this, &SettingsDialog::chooseSlate);
	actions->addWidget(slateChoose);

	slateRemove = new QPushButton(dsrText("Slate.Remove"));
	slateRemove->setObjectName(QStringLiteral("endButton"));
	slateRemove->setCursor(Qt::PointingHandCursor);
	connect(slateRemove, &QPushButton::clicked, this, &SettingsDialog::removeSlate);
	actions->addWidget(slateRemove);
	actions->addStretch();
	body->addLayout(actions);

	slateNote = dsrMakeNote(dsrText("Slate.Note"));
	body->addWidget(slateNote);

	connect(slates, &RelaySlate::changed, this, &SettingsDialog::refreshSlateCard);
	connect(slates, &RelaySlate::failed, this, [this](const QString &errorKey) {
		slateNote->setText(dsrText(errorKey.toUtf8().constData()));
		slateNote->setObjectName(QStringLiteral("destError"));
		dsrRepolish(slateNote);
	});

	refreshSlateCard();
	slates->refresh();
}

/* One file or none, which kind it is, and whether anything is in flight. The
 * note carries the last failure until the next action replaces it. */
void SettingsDialog::refreshSlateCard()
{
	const bool busy = slates->busy();
	const bool have = !slates->current().isEmpty();

	if (busy)
		slateState->setText(dsrText("Slate.Working"));
	else if (!slates->loaded())
		slateState->setText(dsrText("Slate.Loading"));
	else if (have)
		slateState->setText(dsrText(slateSetKey(slates->current().mediaType)));
	else
		slateState->setText(dsrText("Slate.None"));

	slateChoose->setText(dsrText(have ? "Slate.Replace" : "Slate.Choose"));
	slateChoose->setEnabled(!busy);
	slateRemove->setVisible(have);
	slateRemove->setEnabled(!busy);
}

void SettingsDialog::chooseSlate()
{
	const QString filter = QString(dsrText("Slate.Filter")).arg(RelaySlate::fileFilter());
	const QString path = QFileDialog::getOpenFileName(this, dsrText("Slate.PickTitle"), QString(), filter);
	if (path.isEmpty())
		return;

	/* Back to the standing note: whatever went wrong last time is no
	 * longer what the card is reporting. */
	slateNote->setText(dsrText("Slate.Note"));
	slateNote->setObjectName(QStringLiteral("settingsNote"));
	dsrRepolish(slateNote);

	slates->upload(path);
}

void SettingsDialog::removeSlate()
{
	const QMessageBox::StandardButton answer =
		QMessageBox::question(this, dsrText("Slate.RemoveTitle"), dsrText("Slate.RemoveConfirm"));
	if (answer != QMessageBox::Yes)
		return;

	slateNote->setText(dsrText("Slate.Note"));
	slateNote->setObjectName(QStringLiteral("settingsNote"));
	dsrRepolish(slateNote);

	slates->remove();
}
