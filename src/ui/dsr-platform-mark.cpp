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

#include "dsr-platform-mark.hpp"

#include <QChar>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPen>

#include <obs-module.h>

#include "dsr-ui-common.hpp"

namespace {

/* Each platform's own mark colors. The logo art is a white silhouette, so the
 * foreground is recolored per brand rather than assumed: Kick and TikTok
 * publish theirs as dark-on-bright and white-on-black respectively, and
 * white on Kick's green would be illegible. */
struct PlatformMarkStyle {
	QColor background;
	QColor foreground;
};

PlatformMarkStyle platformMarkStyle(const QString &key)
{
	if (key == QLatin1String("twitch"))
		return {QColor(0x91, 0x46, 0xFF), QColor(0xFF, 0xFF, 0xFF)};
	if (key == QLatin1String("youtube"))
		return {QColor(0xFF, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF)};
	if (key == QLatin1String("kick"))
		return {QColor(0x53, 0xFC, 0x18), QColor(0x0B, 0x0B, 0x0B)};
	if (key == QLatin1String("facebook"))
		return {QColor(0x18, 0x77, 0xF2), QColor(0xFF, 0xFF, 0xFF)};
	if (key == QLatin1String("tiktok"))
		return {QColor(0x01, 0x01, 0x01), QColor(0xFF, 0xFF, 0xFF)};
	return {QColor(0x80, 0x80, 0x80), QColor(0xFF, 0xFF, 0xFF)};
}

} // namespace

QString dsrPlatformKey(const QString &platform)
{
	if (platform == QLatin1String("twitch") || platform == QLatin1String("youtube") ||
	    platform == QLatin1String("kick") || platform == QLatin1String("tiktok"))
		return platform;
	if (platform == QLatin1String("facebook") || platform == QLatin1String("facebook_reels"))
		return QStringLiteral("facebook");
	return QString();
}

QString dsrPlatformName(const QString &platform)
{
	if (platform == QLatin1String("twitch"))
		return QStringLiteral("Twitch");
	if (platform == QLatin1String("youtube"))
		return QStringLiteral("YouTube");
	if (platform == QLatin1String("kick"))
		return QStringLiteral("Kick");
	if (platform == QLatin1String("tiktok"))
		return QStringLiteral("TikTok");
	if (platform == QLatin1String("facebook"))
		return QStringLiteral("Facebook");
	if (platform == QLatin1String("facebook_reels"))
		return QStringLiteral("Facebook Reels");
	return QString();
}

QString dsrStripPlatformPrefix(const QString &label, const QString &platform)
{
	/* The separator is built from its code point rather than typed: a middle
	 * dot in a source file is a non-ASCII character, and the repository is
	 * ASCII only. */
	const QString prefix = platform + QStringLiteral(" ") + QChar(0x00B7) + QStringLiteral(" ");
	if (label.startsWith(prefix, Qt::CaseInsensitive))
		return label.mid(prefix.size()).trimmed();
	return label.trimmed();
}

/* Cached: the dock rebuilds its rows on every refresh, and decoding and
 * recoloring a PNG per row per second would be waste for artwork that never
 * changes. */
QPixmap dsrPlatformMark(const QString &key, int side, qreal ratio)
{
	static QHash<QString, QPixmap> cache;
	const QString cacheKey = QStringLiteral("%1|%2|%3").arg(key).arg(side).arg(ratio);
	const auto hit = cache.constFind(cacheKey);
	if (hit != cache.constEnd())
		return *hit;

	const PlatformMarkStyle style = platformMarkStyle(key);
	const int pixels = qRound(side * ratio);

	QPixmap mark(pixels, pixels);
	mark.setDevicePixelRatio(ratio);
	mark.fill(Qt::transparent);

	QPainter painter(&mark);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
	painter.setPen(Qt::NoPen);
	painter.setBrush(style.background);
	painter.drawRoundedRect(QRectF(0, 0, side, side), 7, 7);

	QImage logo;
	if (!key.isEmpty()) {
		char *path = obs_module_file(QStringLiteral("images/platform/%1.png").arg(key).toUtf8().constData());
		if (path) {
			logo.load(QString::fromUtf8(path));
			bfree(path);
		}
	}

	if (!logo.isNull()) {
		/* The art is white with alpha; SourceIn keeps the shape and
		 * replaces the color, so one asset serves every brand. */
		const int glyph = qRound(side * 0.62);
		QImage scaled = logo.scaled(qRound(glyph * ratio), qRound(glyph * ratio), Qt::KeepAspectRatio,
					    Qt::SmoothTransformation)
					.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		QPainter recolor(&scaled);
		recolor.setCompositionMode(QPainter::CompositionMode_SourceIn);
		recolor.fillRect(scaled.rect(), style.foreground);
		recolor.end();
		scaled.setDevicePixelRatio(ratio);

		const qreal w = scaled.width() / ratio;
		const qreal h = scaled.height() / ratio;
		painter.drawImage(QPointF((side - w) / 2.0, (side - h) / 2.0), scaled);
	} else {
		/* Custom RTMP: a small broadcast glyph rather than an invented
		 * logo or a pair of initials. */
		QPen pen(style.foreground);
		pen.setWidthF(1.6);
		pen.setCapStyle(Qt::RoundCap);
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		const QPointF center(side / 2.0, side / 2.0);
		for (int i = 1; i <= 2; i++) {
			const qreal r = i * 4.0;
			painter.drawArc(QRectF(center.x() - r, center.y() - r, r * 2, r * 2), 30 * 16, 120 * 16);
			painter.drawArc(QRectF(center.x() - r, center.y() - r, r * 2, r * 2), 210 * 16, 120 * 16);
		}
		painter.setPen(Qt::NoPen);
		painter.setBrush(style.foreground);
		painter.drawEllipse(center, 1.9, 1.9);
	}
	painter.end();

	cache.insert(cacheKey, mark);
	return mark;
}
