#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <functional>

#include "core/tracking.hpp"
#include "ui/theme.hpp"

class QWidget;

/// @file
/// Seguimiento's forms (1.1), each in an Overlay over the panel. They change
/// nothing themselves: they read the lists, and hand what was typed to a
/// callback, so Panel decides what saving means (and checks that what was
/// edited still exists after a sync).
namespace trackdlg {

/// What the session form produced.
struct SessionDraft {
    QString task;
    QString project;
    qint64 startMs = 0;
    qint64 endMs = 0;      ///< 0: it is the running one and keeps running.
    qint64 pausedMs = 0;
    double rate = -1;      ///< Its own if the project did not change, else the project's.
};

/// Adds (@p session null) or edits a session. A new one proposes, as its
/// start, the end of the last session and, as its end, now: it is for filling
/// in the gap that was forgotten. Overlaps warn but do not block.
/// @p remove is offered only when editing.
void editSession(const Theme *theme, QWidget *host, const Session *session,
                 const QList<Session *> &sessions, const QList<Project *> &projects,
                 std::function<void(const SessionDraft &)> commit,
                 std::function<void()> remove);

struct ProjectDraft {
    QString name;
    int color = 0;
    QString client;
    double rate = -1;
    bool archived = false;
};

/// Creates (@p project null) or edits a project. Only the name is required.
void editProject(const Theme *theme, QWidget *host, const Project *project,
                 const QList<Project *> &projects, std::function<void(const ProjectDraft &)> commit);

/// Asks whether a new rate applies to the sessions already logged. @p pastCount
/// is how many there are; @p answer gets true for "them too".
void askRateScope(const Theme *theme, QWidget *host, const QString &project, double rate,
                  int pastCount, std::function<void(bool)> answer);

/// The CSV export: it starts on the period that was on screen.
void exportCsv(const Theme *theme, QWidget *host, const QList<Session *> &sessions,
               const QList<Project *> &projects, const QDate &from, const QDate &to,
               const QString &project);

}  // namespace trackdlg
