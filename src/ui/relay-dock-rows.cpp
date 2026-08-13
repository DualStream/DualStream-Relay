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

/* Destination rows: the platform mark, the row widget, and the list rebuild. */

#include "relay-dock.hpp"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <obs-module.h>

#include "../relay-secrets.hpp"
#include "dsr-platform-mark.hpp"
#include "dsr-ui-common.hpp"
#include "dsr-widgets.hpp"
#include "relay-dock-text.hpp"

namespace {

/* What the row says beside the logo: the account name on its own, or the
 * platform when there is no label to show. A label the user typed is left
 * exactly as they typed it. */
QString platformDisplay(const DsrDestination &dest)
{
	const QString label = dsrStripPlatformPrefix(dest.label, dest.platform);
	if (!label.isEmpty())
		return label;

	const QString name = dsrPlatformName(dest.platform);
	if (!name.isEmpty())
		return name;
	if (!dest.customPlatform.isEmpty())
		return dest.customPlatform;
	return dsrText("Destinations.Preset.Generic");
}

/* Which platform a row represents, for artwork and colors. A custom RTMP
 * destination can name a platform we have a logo for, so it is consulted
 * too. */
QString platformKey(const DsrDestination &dest)
{
	return dsrPlatformKey(dest.type == QLatin1String("custom") && !dest.customPlatform.isEmpty()
				      ? dest.customPlatform
				      : dest.platform);
}

} // namespace

/* Ask before changing a destination mid-stream. Turning one off takes a
 * platform down for people already watching it, and turning one on puts the
 * stream somewhere the user may not have meant it to go, so neither happens on
 * a single stray click. Off-air the switch stays immediate. */
bool RelayDock::confirmToggleWhileLive(const DsrDestination &dest, bool wanted)
{
	if (current != State::Live && current != State::Protected)
		return true;

	QMessageBox box(this);
	box.setWindowTitle(dsrText(wanted ? "Destinations.ToggleOnTitle" : "Destinations.ToggleOffTitle"));
	box.setText(QString(dsrText(wanted ? "Destinations.ToggleOnConfirm" : "Destinations.ToggleOffConfirm"))
			    .arg(platformDisplay(dest)));
	QPushButton *confirm =
		box.addButton(dsrText(wanted ? "Destinations.ToggleOnAction" : "Destinations.ToggleOffAction"),
			      QMessageBox::AcceptRole);
	box.addButton(dsrText("Button.Cancel"), QMessageBox::RejectRole);
	box.setDefaultButton(confirm);
	box.exec();
	return box.clickedButton() == confirm;
}

void RelayDock::requestToggle(const DsrDestination &dest, bool wanted)
{
	if (!confirmToggleWhileLive(dest, wanted)) {
		/* Put the switch back. The row is rebuilt from the list, so what
		 * lands on screen is the value the server last gave us. */
		refreshUi();
		return;
	}

	const QString id = dest.id;
	pendingToggles.insert(id, {wanted, true});
	refreshUi();

	QJsonObject body;
	body.insert(QStringLiteral("enabled"), wanted);
	destinations->modify(id, body, [this, id](bool ok, QString errorKey) {
		const auto entry = pendingToggles.find(id);
		if (entry == pendingToggles.end())
			return;

		if (ok) {
			/* Accepted. The chosen value stays on screen until the
			 * refreshed list carries it, which is where rebuildRows
			 * drops the entry. */
			entry->inFlight = false;
		} else {
			pendingToggles.erase(entry);
			QMessageBox::warning(this, dsrText("Dock.Title"), dsrText(errorKey.toUtf8().constData()));
		}
		refreshUi();
	});
}

DsrSwitch *RelayDock::makeDestToggle(const DsrDestination &dest)
{
	const auto pending = pendingToggles.constFind(dest.id);
	const bool hasPending = pending != pendingToggles.constEnd();

	DsrSwitch *toggle = new DsrSwitch;
	toggle->setChecked(hasPending ? pending->enabled : dest.enabled);
	toggle->setEnabled(!(hasPending && pending->inFlight));
	toggle->setToolTip(dsrText("Destinations.EnabledTip"));

	/* By value: the list this came from is replaced wholesale on every
	 * refresh, and the row outlives the loop that built it. */
	const DsrDestination copy = dest;
	connect(toggle, &QAbstractButton::clicked, this, [this, copy](bool checked) { requestToggle(copy, checked); });
	return toggle;
}

/* Forget a pending toggle once the list agrees with it, or once the
 * destination it belonged to is gone. */
void RelayDock::prunePendingToggles()
{
	for (auto it = pendingToggles.begin(); it != pendingToggles.end();) {
		const DsrDestination *dest = nullptr;
		for (const DsrDestination &candidate : destinations->list()) {
			if (candidate.id == it.key()) {
				dest = &candidate;
				break;
			}
		}

		if (!dest || (!it->inFlight && it->enabled == dest->enabled))
			it = pendingToggles.erase(it);
		else
			++it;
	}
}

void RelayDock::rebuildRows()
{
	prunePendingToggles();

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
	row->setObjectName(QStringLiteral("destRow"));
	row->setCursor(Qt::PointingHandCursor);
	row->setProperty("destId", dest.id);
	row->setToolTip(dsrText("Destinations.RowTip"));
	row->installEventFilter(this);
	QVBoxLayout *outer = new QVBoxLayout(row);
	outer->setContentsMargins(10, 8, 8, 8);
	outer->setSpacing(4);

	QWidget *line = new QWidget;
	QHBoxLayout *lineLayout = new QHBoxLayout(line);
	lineLayout->setContentsMargins(0, 0, 0, 0);
	lineLayout->setSpacing(9);

	QLabel *mark = new QLabel;
	mark->setPixmap(dsrPlatformMark(platformKey(dest), 28, devicePixelRatioF()));
	mark->setFixedSize(28, 28);
	lineLayout->addWidget(mark);

	QLabel *name = new QLabel(platformDisplay(dest));
	name->setObjectName(QStringLiteral("destName"));
	/* Long account names give way to the controls rather than stretching
	 * the row past the dock's width. */
	name->setTextInteractionFlags(Qt::NoTextInteraction);
	name->setMinimumWidth(0);
	lineLayout->addWidget(name, 1);

	/* A switch the user has moved says so until the server answers, since
	 * that is the only thing on the row that has changed yet. Otherwise,
	 * while live, the state word replaces the canvas badge: the canvas is a
	 * setup detail and the state is what matters mid-stream. */
	const auto pending = pendingToggles.constFind(dest.id);
	if (pending != pendingToggles.constEnd() && pending->inFlight) {
		QLabel *state = new QLabel(dsrText("Destinations.Applying"));
		state->setObjectName(QStringLiteral("destState"));
		state->setProperty("state", QStringLiteral("applying"));
		lineLayout->addWidget(state);
	} else if (live && !live->state.isEmpty()) {
		QLabel *state = new QLabel;
		state->setObjectName(QStringLiteral("destState"));
		state->setProperty("state", live->state);
		const QByteArray stateKey = "DestState." + live->state.toUtf8();
		state->setText(dsrText(stateKey.constData()));
		lineLayout->addWidget(state);
	} else {
		QLabel *canvas = new QLabel(dsrCanvasDisplay(dest.canvas));
		canvas->setObjectName(QStringLiteral("canvasBadge"));
		lineLayout->addWidget(canvas);
	}

	lineLayout->addWidget(makeDestToggle(dest));

	const QString id = dest.id;

	DsrIconButton *menuButton = new DsrIconButton(DsrIconButton::Glyph::Kebab);
	QMenu *menu = new QMenu(menuButton);

	menu->addAction(dsrText("Destinations.Edit"), this, [this, id]() { openEditDialog(id); });

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

	menu->addSeparator();
	menu->addAction(dsrText("Destinations.Remove"), this, [this, id]() {
		const QMessageBox::StandardButton answer = QMessageBox::question(
			this, dsrText("Destinations.RemoveTitle"), dsrText("Destinations.RemoveConfirm"));
		if (answer != QMessageBox::Yes)
			return;
		/* The destination is going, so the key kept for it goes too. */
		dsrSecretForget(id);
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
