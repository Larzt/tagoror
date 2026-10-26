#pragma once

#include <QDate>
#include <QList>
#include <QWidget>

#include "core/tracking.hpp"
#include "ui/theme.hpp"

class QCompleter;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QToolButton;

namespace track {
class ClockCard;
class LogModel;
class LogView;
class ProjectModel;
class ProjectView;
class DayBars;
class Breakdown;
}

/// The Seguimiento page (1.1): the clock that adds up. On top, always ready,
/// the stopwatch — type what you are on and press Enter, or click a recent
/// one —; under it three tabs: Registro (sessions grouped by day, with
/// today's line from 8 to 20 h), Resumen (the week or the month as stacked
/// bars per project, hours and the estimated amount) and Proyectos. From
/// kWideFrom up the stopwatch takes the whole width and Registro and Resumen
/// sit side by side; Proyectos stays a tab.
///
/// Like the other pages it stores nothing and changes no state itself: it
/// draws the Store's lists and asks Panel for every change. refresh()
/// rebuilds the lists; tick() only repaints what counts, every second while
/// a session runs, so typing in the stopwatch's field is never interrupted.
class TrackingView : public QWidget {
    Q_OBJECT

public:
    enum Tab { Log, Summary, Projects };
    /// The page's own width at which Registro and Resumen go side by side: a
    /// 720px panel, less the rail.
    static constexpr int kWideFrom = 640;

    explicit TrackingView(const Theme &theme, QWidget *parent = nullptr);

    void setSources(const QList<Session *> *sessions, const QList<Project *> *projects);
    void setTheme(const Theme &theme);
    void refresh();
    void retranslate();
    void tick();

    Tab tab() const { return m_tab; }
    void setTab(Tab tab);
    /// Puts the cursor in "¿En qué estás trabajando?".
    void focusTask();
    /// The project the next session starts with (a project just created).
    void setPendingProject(const QString &id);
    /// The period Resumen shows, which the export starts from.
    QDate periodFrom() const;
    QDate periodTo() const;

signals:
    void startRequested(const QString &task, const QString &project);
    void pauseToggled();
    void stopRequested();
    /// The running session's project, changed from its chip.
    void runningProjectPicked(const QString &project);
    /// Start again what @p s was: same task, same project, from now.
    void relaunchRequested(Session *s);
    /// Null: a new, past session.
    void sessionEditRequested(Session *s);
    void sessionDeleteRequested(Session *s);
    void sessionDuplicateRequested(Session *s);
    void sessionMoveRequested(Session *s, const QString &project);
    /// Null: a new project.
    void projectEditRequested(Project *p);
    void exportRequested(const QDate &from, const QDate &to);

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    QWidget *buildTabs();
    QWidget *buildEmpty();
    QWidget *buildSummary();
    QWidget *buildProjects();
    void refreshTabs();
    void refreshRecents();
    void refreshSummary();
    void applyLayout();
    void openSessionMenu(Session *s, const QPoint &globalPos);
    void openProjectPicker(QWidget *anchor, bool forRunning);
    /// The project last used with @p task, to preselect it.
    QString lastProjectFor(const QString &task) const;
    const Project *project(const QString &id) const;

    Theme m_theme;
    const QList<Session *> *m_sessions = nullptr;
    const QList<Project *> *m_projects = nullptr;
    Tab m_tab = Log;
    bool m_wide = false;
    /// The project the next session starts with. Preselected from the last one
    /// used with the task being typed, unless it was picked by hand.
    QString m_pending;
    bool m_pendingPicked = false;
    QCompleter *m_completer = nullptr;

    QList<QToolButton *> m_tabButtons;
    track::ClockCard *m_clock = nullptr;
    QWidget *m_recents = nullptr;
    QGridLayout *m_recentGrid = nullptr;
    QWidget *m_split = nullptr;            ///< Registro and Resumen, side by side when wide.
    QStackedWidget *m_logStack = nullptr;  ///< Empty state / the log.
    track::LogModel *m_logModel = nullptr;
    track::LogView *m_logView = nullptr;
    QScrollArea *m_summary = nullptr;
    QWidget *m_projectsPane = nullptr;
    track::ProjectModel *m_projectModel = nullptr;
    track::ProjectView *m_projectView = nullptr;
    QLabel *m_emptyTitle = nullptr;
    QLabel *m_emptyText = nullptr;
    QLabel *m_emptyHint = nullptr;
    QToolButton *m_emptyProject = nullptr;
    QToolButton *m_emptyPast = nullptr;
    QToolButton *m_newProject = nullptr;

    /// @name Resumen
    /// @{
    bool m_month = false;
    QDate m_anchor = QDate::currentDate();   ///< A day inside the period shown.
    QToolButton *m_weekBtn = nullptr;
    QToolButton *m_monthBtn = nullptr;
    QLabel *m_periodLabel = nullptr;
    QToolButton *m_csvBtn = nullptr;
    QLabel *m_hoursCaption = nullptr;
    QLabel *m_hoursValue = nullptr;
    QLabel *m_hoursSub = nullptr;
    QLabel *m_moneyCaption = nullptr;
    QLabel *m_moneyValue = nullptr;
    QLabel *m_moneySub = nullptr;
    QLabel *m_daySection = nullptr;
    QLabel *m_projectSection = nullptr;
    track::DayBars *m_bars = nullptr;
    track::Breakdown *m_breakdown = nullptr;
    /// @}
};
