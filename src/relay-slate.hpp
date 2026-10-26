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

#pragma once

#include <QObject>
#include <QString>

#include "relay-auth.hpp"

/* The standby screen.
 *
 * While disconnect protection holds a stream up, the relay plays a picture in
 * place of the feed. Without one of the account's own it plays the DualStream
 * default. This is how the account's own is set.
 *
 * One picture per account. Uploading replaces whatever was there, and the
 * relay re-renders it for each canvas at the next go-live, fitted inside the
 * frame, so a single upload serves both the desktop and the mobile stream. */

struct DsrSlate {
	QString id;
	/* "image", "video" or "gif", decided by the server from the file. */
	QString mediaType;

	bool isEmpty() const { return id.isEmpty(); }
};

class RelaySlate : public QObject {
	Q_OBJECT

public:
	explicit RelaySlate(RelayAuth *auth, QObject *parent = nullptr);

	const DsrSlate &current() const { return slate; }
	bool loaded() const { return loadedFlag; }
	/* An upload or a removal is in flight; the card disables itself. */
	bool busy() const { return busyFlag; }

	void refresh();
	void upload(const QString &filePath);
	void remove();

	/* The file types the relay can render, as a file dialog filter. */
	static QString fileFilter();
	/* Content type for a path's extension, empty when it is not one the
	 * relay takes. */
	static QByteArray contentTypeFor(const QString &filePath);
	static qint64 maxBytes();

signals:
	void changed();
	/* Locale key for what went wrong; the card shows it in place of the
	 * note until the next action. */
	void failed(const QString &errorKey);

private:
	void requestUploadUrl(const QString &filePath, const QByteArray &contentType);
	void putBytes(const QString &uploadUrl, const QString &id, const QByteArray &bytes,
		      const QByteArray &contentType);
	void commit(const QString &id, const QByteArray &contentType);
	void fail(const QString &errorKey);

	RelayAuth *auth;
	DsrSlate slate;
	bool loadedFlag = false;
	bool busyFlag = false;
};
