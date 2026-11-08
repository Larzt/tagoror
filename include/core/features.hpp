#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "core/note.hpp"

/// @file
/// The parts of Tagoror that can be left out (1.3.0). A first start asks
/// which ones to use (WelcomeView) and Settings → General → Funciones changes
/// them later. Notes and text notes are the app itself and are always there.
///
/// Leaving one out hides it — its page in the navigation, its shortcuts, its
/// commands, its type in "+" — and silences what it would ring. Nothing is
/// deleted: turning it back on brings back everything it had.
///
/// Stored as the keys turned *off* (Store::Prefs::featuresOff), so a file
/// from before 1.3 has everything on, as it always had, and a feature added
/// later starts on.
namespace Features {

enum Id { Planner, Timers, Birthdays, Tracking, Checklists, Reminders, Voice, Markdown, Collections };

struct Info {
    Id id;
    const char *key;    ///< Stored in notes.json: never translated or renamed.
    const char *icon;   ///< paintIcon() kind.
    const char *name;   ///< Spanish; translated when shown.
    const char *hint;   ///< Spanish; translated when shown.
    bool page;          ///< A page of its own, or a note type.
};

/// In the order they are offered: pages first, as in the navigation, then note
/// types, as in "+".
inline const QList<Info> &all() {
    static const QList<Info> list{
        {Planner, "planner", "calendar", "Planner",
         "Eventos y tareas por día, semana y mes", true},
        {Timers, "timers", "timer", "Timers",
         "Cuentas atrás y Pomodoro que suenan al acabar", true},
        {Birthdays, "birthdays", "cake", "Cumpleaños",
         "Fechas que avisan cada año", true},
        {Tracking, "tracking", "bars", "Seguimiento",
         "El tiempo que dedicas a cada proyecto", true},
        {Collections, "collections", "books", "Colecciones",
         "Libros por leer, leyendo y leídos, con su cola", true},
        {Checklists, "checklists", "check", "Checklists",
         "Notas con casillas para marcar", false},
        {Reminders, "reminders", "bell", "Recordatorios",
         "Notas que suenan a una hora", false},
        {Voice, "voice", "mic", "Notas de voz",
         "Grabaciones con el micrófono", false},
        {Markdown, "markdown", "markdown", "Notas Markdown",
         "Títulos, listas y enlaces en su propia página", false},
    };
    return list;
}

inline QString key(Id id) {
    for (const Info &i : all())
        if (i.id == id) return QString::fromLatin1(i.key);
    return QString();
}

inline bool enabled(const QStringList &off, Id id) { return !off.contains(key(id)); }

/// The feature a note type belongs to, or -1 for a text note, which is always on.
inline int forNoteType(Note::Type type) {
    switch (type) {
        case Note::Check:    return Checklists;
        case Note::Reminder: return Reminders;
        case Note::Voice:    return Voice;
        case Note::Markdown: return Markdown;
        default:             return -1;
    }
}

inline bool noteTypeEnabled(const QStringList &off, Note::Type type) {
    const int f = forNoteType(type);
    return f < 0 || enabled(off, Id(f));
}

}  // namespace Features
