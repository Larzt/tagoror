#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>

// Un área de trabajo: una pestaña de la lista de notas (Personal, Máster,
// Stride…). Cada nota pertenece a una, y la lista solo enseña las del área
// abierta. No tienen color propio a propósito: el acento tiene que seguir
// siendo el único color fuerte del panel, y el rojo es de lo que suena. Lo que
// las distingue es un glifo geométrico monocromo, opcional.
//
// Viven en notes.json bajo "areas" y se sincronizan como un elemento más (su
// fecha de cambio, sus lápidas "a:<id>"). El orden de las pestañas es 'pos' y
// no el de la lista: así reordenar es cambiar un campo de cada área, que es lo
// que la mezcla elemento a elemento sabe llevar entre equipos.
struct Area {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    QString glyph;            // "" (ninguno), o uno de glyphs()
    int pos = 0;              // lugar en la tira de pestañas
    qint64 updatedMs = 0;     // ver Note::updatedMs

    // La que se crea sola para las notas que ya existían antes de las áreas.
    // Tiene id fijo para que dos equipos que la crean cada uno por su lado
    // acaben con la misma y no con dos "Personal". Las notas de esa área no
    // guardan la clave "area" (ver Note::area), así que no cambian de bytes.
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
