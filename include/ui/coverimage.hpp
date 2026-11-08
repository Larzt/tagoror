#pragma once

#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <QString>

/// @file
/// A collection item's cover image (1.3.4), drawn the way the coloured covers
/// are: filling the cover's rounded shape, cropped rather than squeezed.
/// Shared by the page (rows, tiles, cards, the detail) and the item's editor.

namespace coverimage {

/// @p path scaled to cover @p size (device pixels) and cropped to its centre.
/// Cached per file, its modification and the size: a grid of covers repaints
/// on every hover, and decoding a JPEG each time would show.
inline QPixmap pixmap(const QString &path, const QSize &size) {
    if (path.isEmpty() || size.isEmpty()) return QPixmap();
    const QFileInfo info(path);
    if (!info.exists()) return QPixmap();
    const QString key = QString("cover:%1:%2:%3x%4")
                            .arg(path)
                            .arg(info.lastModified().toMSecsSinceEpoch())
                            .arg(size.width())
                            .arg(size.height());
    QPixmap out;
    if (QPixmapCache::find(key, &out)) return out;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) return QPixmap();
    const QImage scaled = image.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    out = QPixmap::fromImage(scaled.copy((scaled.width() - size.width()) / 2,
                                         (scaled.height() - size.height()) / 2, size.width(), size.height()));
    QPixmapCache::insert(key, out);
    return out;
}

/// Fills @p shape (whose bounds are @p r) with the image at @p path. Returns
/// false, drawing nothing, if there is no image to draw: the caller paints
/// the colour instead.
inline bool draw(QPainter &p, const QPainterPath &shape, const QRectF &r, const QString &path) {
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
    const QSize size = (r.size() * dpr).toSize();
    QPixmap pm = pixmap(path, size);
    if (pm.isNull()) return false;
    pm.setDevicePixelRatio(dpr);
    p.save();
    p.setClipPath(shape, Qt::IntersectClip);
    p.drawPixmap(r.topLeft(), pm);
    p.restore();
    return true;
}

}  // namespace coverimage
