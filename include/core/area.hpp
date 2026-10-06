#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>

/// A workspace area: one tab of the note list. Every note belongs to one and
/// the list shows only the open area's notes.
///
/// Areas deliberately have no colour of their own (the accent must stay the
/// only strong colour, and red means something is ringing); an optional
/// monochrome glyph tells them apart. Tab order is @ref pos, not list order,
/// so reordering is a per-element change the sync merge can carry.
struct Area {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    QString glyph;            ///< Empty (none) or one of glyphs().
    int pos = 0;              ///< Position in the tab strip.
    qint64 updatedMs = 0;     ///< See Note::updatedMs.

    /// Id of the area created for notes that predate areas. It is fixed so two
    /// machines creating it independently end up with one, and notes in it do not
    /// store the "area" key (see Note::area), so their bytes do not change.
    static constexpr auto kDefaultId = "default";

    static const QStringList &glyphs() {
        static const QStringList list{"dot", "square", "triangle", "diamond", "ring", "bar"};
        return list;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id;
        o["name"] = name;
        o["glyph"] = glyph;
        o["pos"] = pos;
        o["updated"] = double(updatedMs);
        return o;
    }

    static Area *fromJson(const QJsonObject &o) {
        auto *a = new Area;
        a->id = o["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        a->name = o["name"].toString();
        a->glyph = o["glyph"].toString();
        if (!glyphs().contains(a->glyph)) a->glyph.clear();
        a->pos = o["pos"].toInt();
        a->updatedMs = qint64(o["updated"].toDouble());
        return a;
    }
};
