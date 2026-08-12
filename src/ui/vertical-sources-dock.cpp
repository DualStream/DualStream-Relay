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

#include "vertical-sources-dock.hpp"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QVector>

#include <functional>

#include "../vertical-canvas.hpp"
#include "dsr-source-icon.hpp"
#include "dsr-ui-common.hpp"
#include "dsr-widgets.hpp"
#include "vertical-common.hpp"

namespace {

/* Private to this dock, so a drag from anywhere else is refused. */
const char *kRowMime = "application/x-dualstream-vertical-row";

const int kIconSide = 18;

} // namespace

VerticalSourcesDock::VerticalSourcesDock(VerticalCanvas *manager, QWidget *parent) : QWidget(parent), manager(manager)
{
	QVBoxLayout *root = new QVBoxLayout(this);
	root->setContentsMargins(8, 8, 8, 8);
	root->setSpacing(4);

	scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	listContainer = new QWidget;
	listLayout = new QVBoxLayout(listContainer);
	listLayout->setContentsMargins(0, 0, 0, 0);
	listLayout->setSpacing(4);
	listLayout->addStretch(1);
	scroll->setWidget(listContainer);
	root->addWidget(scroll, 1);

	dropLine = new QWidget(listContainer);
	dropLine->setObjectName(QStringLiteral("dropLine"));
	dropLine->setFixedHeight(2);
	dropLine->hide();

	setAcceptDrops(true);

	connect(manager, &VerticalCanvas::changed, this, &VerticalSourcesDock::rebuildRows);
	connect(manager, &VerticalCanvas::selectionChanged, this, &VerticalSourcesDock::updateHighlights);
	connect(manager, &VerticalCanvas::itemVisibilityChanged, this, &VerticalSourcesDock::updateVisibility);
	connect(manager, &VerticalCanvas::itemLockChanged, this, &VerticalSourcesDock::updateLock);

	setStyleSheet(dsrVerticalStyleSheet());
	setMinimumWidth(200);
	rebuildRows();
}

QSize VerticalSourcesDock::sizeHint() const
{
	return QSize(260, 320);
}

void VerticalSourcesDock::updateHighlights(qint64 selectedId)
{
	for (int i = 0; i < listLayout->count(); i++) {
		QWidget *row = listLayout->itemAt(i)->widget();
		if (!row)
			continue;
		const bool match = row->property("itemId").toLongLong() == selectedId;
		if (row->property("selected").toBool() != match) {
			row->setProperty("selected", match);
			dsrRepolish(row);
		}
	}
}

/* Driven by the scene's own item_visible signal, so a visibility change made
 * anywhere else in OBS moves this switch without the list being rebuilt. */
void VerticalSourcesDock::updateVisibility(qint64 itemId, bool visible)
{
	for (int i = 0; i < listLayout->count(); i++) {
		QWidget *row = listLayout->itemAt(i)->widget();
		if (!row || row->property("itemId").toLongLong() != itemId)
			continue;
		QCheckBox *eye = row->findChild<QCheckBox *>(QStringLiteral("rowEye"));
		if (eye && eye->isChecked() != visible) {
			QSignalBlocker blocker(eye);
			eye->setChecked(visible);
		}
		return;
	}
}

void VerticalSourcesDock::updateLock(qint64 itemId, bool locked)
{
	for (int i = 0; i < listLayout->count(); i++) {
		QWidget *row = listLayout->itemAt(i)->widget();
		if (!row || row->property("itemId").toLongLong() != itemId)
			continue;
		QCheckBox *lock = row->findChild<QCheckBox *>(QStringLiteral("rowLock"));
		if (lock && lock->isChecked() != locked) {
			QSignalBlocker blocker(lock);
			lock->setChecked(locked);
		}
		return;
	}
}

obs_sceneitem_t *VerticalSourcesDock::resolveItem(int64_t itemId) const
{
	return dsrFindCounterpartItem(manager, itemId);
}

QWidget *VerticalSourcesDock::makeRow(obs_sceneitem_t *item, int64_t selectedId)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	const int64_t itemId = obs_sceneitem_get_id(item);

	QWidget *row = new QWidget;
	row->setObjectName(QStringLiteral("vertRow"));
	row->setProperty("itemId", QVariant((qlonglong)itemId));
	row->setProperty("selected", itemId == selectedId);
	row->setCursor(Qt::PointingHandCursor);
	row->installEventFilter(this);

	QHBoxLayout *rowLayout = new QHBoxLayout(row);
	rowLayout->setContentsMargins(10, 3, 5, 3);
	rowLayout->setSpacing(8);

	QLabel *icon = new QLabel;
	icon->setProperty("class", QStringLiteral("source-icon"));
	icon->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	icon->setPixmap(
		dsrSourceIcon(obs_source_get_icon_type(obs_source_get_id(source))).pixmap(kIconSide, kIconSide));

	QLabel *name = new QLabel(QString::fromUtf8(obs_source_get_name(source)));
	name->setObjectName(QStringLiteral("vertName"));

	/* Eye then padlock, the same widgets OBS builds for its own list, so
	 * the active theme draws them with its own artwork. */
	QCheckBox *eye = dsrMakeVisibilityCheck();
	eye->setObjectName(QStringLiteral("rowEye"));
	eye->setChecked(obs_sceneitem_visible(item));
	eye->setToolTip(dsrText("Vertical.VisibleTip"));
	connect(eye, &QAbstractButton::clicked, this, [this, itemId](bool checked) {
		obs_sceneitem_t *target = resolveItem(itemId);
		if (target) {
			obs_sceneitem_set_visible(target, checked);
			obs_sceneitem_release(target);
		}
	});

	QCheckBox *lock = dsrMakeLockCheck();
	lock->setObjectName(QStringLiteral("rowLock"));
	lock->setChecked(obs_sceneitem_locked(item));
	lock->setToolTip(dsrText("Vertical.LockTip"));
	connect(lock, &QAbstractButton::clicked, this, [this, itemId](bool checked) {
		obs_sceneitem_t *target = resolveItem(itemId);
		if (target) {
			obs_sceneitem_set_locked(target, checked);
			obs_sceneitem_release(target);
		}
	});

	DsrIconButton *kebab = new DsrIconButton(DsrIconButton::Glyph::Kebab);
	QMenu *menu = new QMenu(kebab);
	auto itemAction = [this, itemId](std::function<void(obs_sceneitem_t *)> action) {
		return [this, itemId, action]() {
			obs_sceneitem_t *target = resolveItem(itemId);
			if (target) {
				action(target);
				obs_sceneitem_release(target);
			}
		};
	};
	/* Ordering is done by dragging the row, so the menu carries only the
	 * two arrangement actions. */
	menu->addAction(dsrText("Vertical.Fill"), this,
			itemAction([](obs_sceneitem_t *target) { VerticalCanvas::placeItem(target, true); }));
	menu->addAction(dsrText("Vertical.Fit"), this,
			itemAction([](obs_sceneitem_t *target) { VerticalCanvas::placeItem(target, false); }));
	kebab->setMenu(menu);

	rowLayout->addWidget(icon);
	rowLayout->addWidget(name, 1);
	rowLayout->addWidget(eye);
	rowLayout->addWidget(lock);
	rowLayout->addWidget(kebab);
	return row;
}

void VerticalSourcesDock::rebuildRows()
{
	const int64_t keepSelected = manager->selectedItemId();

	dropLine->hide();
	while (listLayout->count() > 0) {
		QLayoutItem *entry = listLayout->takeAt(0);
		if (entry->widget())
			entry->widget()->deleteLater();
		delete entry;
	}

	const char *emptyKey = nullptr;
	obs_source_t *sceneSource = nullptr;
	if (!manager->enabled())
		emptyKey = "Vertical.SourcesOff";
	else if (!(sceneSource = manager->currentCounterpart()))
		emptyKey = "Vertical.NoScene";

	if (emptyKey) {
		QLabel *empty = new QLabel(dsrText(emptyKey));
		empty->setObjectName(QStringLiteral("mutedText"));
		empty->setWordWrap(true);
		listLayout->addWidget(empty);
		listLayout->addStretch(1);
		return;
	}

	QVector<obs_sceneitem_t *> items;
	obs_scene_enum_items(obs_scene_from_source(sceneSource), dsrCollectSceneItems, &items);
	obs_source_release(sceneSource);

	/* Topmost first, matching how the stack reads visually. */
	for (int i = items.size() - 1; i >= 0; i--)
		listLayout->addWidget(makeRow(items[i], keepSelected));

	listLayout->addStretch(1);

	for (obs_sceneitem_t *item : items)
		obs_sceneitem_release(item);
}

/* ---- reordering by drag -------------------------------------------------- */

void VerticalSourcesDock::startDrag(QWidget *row)
{
	QMimeData *mime = new QMimeData;
	mime->setData(QString::fromUtf8(kRowMime), QByteArray::number(row->property("itemId").toLongLong()));

	QDrag *drag = new QDrag(this);
	drag->setMimeData(mime);
	drag->setPixmap(row->grab());
	drag->setHotSpot(QPoint(row->width() / 2, row->height() / 2));
	drag->exec(Qt::MoveAction);
}

/* Index in the row list a drop at this point would land before. Measured
 * against each row's midpoint, which is what makes the indicator settle
 * either side of the row the cursor is over. */
int VerticalSourcesDock::dropRowAt(const QPoint &dockPos) const
{
	const QPoint local = listContainer->mapFrom(const_cast<VerticalSourcesDock *>(this), dockPos);
	int index = 0;
	for (int i = 0; i < listLayout->count(); i++) {
		QWidget *row = listLayout->itemAt(i)->widget();
		if (!row || !row->property("itemId").isValid())
			continue;
		if (local.y() < row->y() + row->height() / 2)
			return index;
		index++;
	}
	return index;
}

void VerticalSourcesDock::showDropIndicator(int row)
{
	int index = 0;
	int y = 0;
	for (int i = 0; i < listLayout->count(); i++) {
		QWidget *widget = listLayout->itemAt(i)->widget();
		if (!widget || !widget->property("itemId").isValid())
			continue;
		if (index == row) {
			y = widget->y() - 3;
			break;
		}
		y = widget->y() + widget->height() + 1;
		index++;
	}

	dropLine->setGeometry(0, y, listContainer->width(), 2);
	dropLine->raise();
	dropLine->show();
}

void VerticalSourcesDock::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasFormat(QString::fromUtf8(kRowMime)))
		event->acceptProposedAction();
}

void VerticalSourcesDock::dragMoveEvent(QDragMoveEvent *event)
{
	if (!event->mimeData()->hasFormat(QString::fromUtf8(kRowMime)))
		return;
	showDropIndicator(dropRowAt(event->position().toPoint()));
	event->acceptProposedAction();
}

void VerticalSourcesDock::dragLeaveEvent(QDragLeaveEvent *)
{
	dropLine->hide();
}

void VerticalSourcesDock::dropEvent(QDropEvent *event)
{
	dropLine->hide();
	if (!event->mimeData()->hasFormat(QString::fromUtf8(kRowMime)))
		return;

	const qint64 itemId = event->mimeData()->data(QString::fromUtf8(kRowMime)).toLongLong();
	applyDrop(itemId, dropRowAt(event->position().toPoint()));
	event->acceptProposedAction();
}

/* The list runs topmost first while obs_sceneitem_set_order_position counts
 * from the bottom, so the target row is mirrored before it is applied. The
 * scene's reorder signal rebuilds the list afterwards. */
void VerticalSourcesDock::applyDrop(qint64 itemId, int targetRow)
{
	obs_source_t *sceneSource = manager->currentCounterpart();
	if (!sceneSource)
		return;

	QVector<obs_sceneitem_t *> items;
	obs_scene_enum_items(obs_scene_from_source(sceneSource), dsrCollectSceneItems, &items);
	obs_source_release(sceneSource);

	const int count = items.size();
	obs_sceneitem_t *moved = nullptr;
	int currentRow = 0;
	for (int i = count - 1; i >= 0; i--) {
		if (obs_sceneitem_get_id(items[i]) == itemId) {
			moved = items[i];
			break;
		}
		currentRow++;
	}

	if (moved) {
		/* Dropping below its own position shifts the target up by one,
		 * because the item leaves the list before it is reinserted. */
		int row = targetRow > currentRow ? targetRow - 1 : targetRow;
		row = qBound(0, row, count - 1);
		if (row != currentRow)
			obs_sceneitem_set_order_position(moved, count - 1 - row);
	}

	for (obs_sceneitem_t *item : items)
		obs_sceneitem_release(item);
}

bool VerticalSourcesDock::eventFilter(QObject *watched, QEvent *event)
{
	QWidget *row = qobject_cast<QWidget *>(watched);
	if (!row || !row->property("itemId").isValid())
		return QWidget::eventFilter(watched, event);

	if (event->type() == QEvent::MouseButtonPress) {
		QMouseEvent *me = static_cast<QMouseEvent *>(event);
		if (me->button() == Qt::LeftButton) {
			pressOrigin = me->position().toPoint();
			const qint64 id = row->property("itemId").toLongLong();
			obs_sceneitem_t *target = resolveItem(id);
			const bool locked = target && obs_sceneitem_locked(target);
			obs_sceneitem_release(target);
			pressedItemId = locked ? -1 : id;
		}
		return false;
	}

	if (event->type() == QEvent::MouseMove && pressedItemId >= 0) {
		QMouseEvent *me = static_cast<QMouseEvent *>(event);
		if ((me->buttons() & Qt::LeftButton) &&
		    (me->position().toPoint() - pressOrigin).manhattanLength() >= QApplication::startDragDistance()) {
			pressedItemId = -1;
			startDrag(row);
			return true;
		}
		return false;
	}

	if (event->type() == QEvent::MouseButtonRelease) {
		QMouseEvent *me = static_cast<QMouseEvent *>(event);
		pressedItemId = -1;
		if (me->button() == Qt::LeftButton && row->rect().contains(me->position().toPoint())) {
			manager->setSelectedItemId(row->property("itemId").toLongLong());
			return true;
		}
	}

	return QWidget::eventFilter(watched, event);
}
