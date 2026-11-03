#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QWidget>

#include "core/birthday.hpp"
#include "core/event.hpp"
#include "core/note.hpp"
#include "ui/theme.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QScrollArea;
class QStackedWidget;
class QTextEdit;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;

class TimeGrid;
class MonthBoard;
class MiniMonth;

/// The planner page, behind the calendar header button: day, week and month
/// views, a side panel (mini month, today's tasks, category filter) and an
/// in-page form to create and edit.
///
/// It shows three sources on one grid: events and tasks (events.json, its own
/// data), scheduled reminder notes (owned by the list, only drawn here) and
/// birthdays (all-day). Like the other pages it stores nothing and asks Panel
/// for every change.
///
/// A week does not fit in the usual 300px, so the window widens on entering
/// (Panel::enterWide()) and narrows on leaving. If it is still narrow, the
/// side panel hides and the toolbar takes two rows.
class PlannerView : public QWidget {
    Q_OBJECT

public:
    enum View { Day, Week, Month };

    /// Width at which it fits whole: side panel plus a readable week.
    static constexpr int kPreferredWidth = 780;

    explicit PlannerView(const Theme &theme, QWidget *parent = nullptr);

    /// Everything belongs to the Store: pointers to its lists are kept and read
    /// on every repaint.
    void setSources(const QList<Event *> *events, const QList<Note *> *notes,
                    const QList<Birthday *> *birthdays,
                    const QList<Event::Category *> *categories);
    void setTheme(const Theme &theme);
    void setView(View v);
    View view() const { return m_view; }
    void setHidden(const QStringList &categories);
    QStringList hidden() const { return QStringList(m_hidden.begin(), m_hidden.end()); }

    void refresh();
    void retranslate();
    void goTo(const QDate &day);
    /// Closes the form if open.
    /// @return Whether it was: Escape closes the form first, and only a second
    ///         press leaves the page.
    bool closeEditor();

    /// Ctrl+C, Ctrl+V and Ctrl+D. They act on what is under the pointer, or on
    /// the block with keyboard focus; in the form, Ctrl+D duplicates the event
    /// being edited and the other two are left to its text fields.
    void copyShortcut();
    void pasteShortcut();
    void duplicateShortcut();

    /// Filterable categories that are not in events.json.
    static constexpr auto kReminders = "reminders";
    static constexpr auto kBirthdays = "birthdays";

    /// What is drawn: an entry from any of the three sources, placed on a day.
    struct Item {
        enum Source { FromEvent, FromReminder, FromBirthday };
        Source source = FromEvent;
        Event *event = nullptr;
        Note *note = nullptr;
        Birthday *birthday = nullptr;
        QDate day;
        qreal start = 0;      ///< Decimal hours.
        qreal end = 0;
        QString title;
        QColor color;
        bool task = false;
        bool done = false;
        bool alert = false;   ///< Ringing, or a non-repeating reminder already past.
        bool allDay = false;
    };
    QList<Item> itemsOn(const QDate &day) const;

    /// A place on the visible view: the day (and, on the grid, the hour) under
    /// a point, and the item there if any.
    struct Spot {
        bool valid = false;
        bool onItem = false;
        Item item;
        QDate day;
        qreal hour = -1;      ///< -1 off the grid: the pasted item keeps its time.
    };

    /// Built-in categories followed by the user's, with display names. @c custom
    /// is null for built-in ones.
    struct CatInfo {
        QString id;
        QString name;
        QColor color;
        Event::Category *custom = nullptr;
    };
    QList<CatInfo> allCategories() const;
    QColor categoryColor(const QString &id) const;

    /// Colours offered when creating a category.
    static const QList<QColor> &categoryPalette();

signals:
    void eventCreated(Event *e);           ///< Ownership passes to the receiver.
    void eventChanged(Event *e);
    void eventDeleted(Event *e);
    void reminderCreated(const QString &title, const QDateTime &when);
    /// A reminder dragged to another moment, with its new dueAtMs.
    void reminderMoved(Note *n, qint64 dueAtMs);
    /// A copy of a reminder pasted: @p snapshot is the original as it was when
    /// copied (it may have been deleted since), to be created at @p dueAtMs.
    void reminderPasted(const QJsonObject &snapshot, qint64 dueAtMs);
    void noteActivated(Note *n);
    void birthdayActivated(Birthday *b);
    void viewChanged(int view);
    void hiddenChanged(const QStringList &categories);
    /// Custom categories. Ownership of a new one passes to the receiver; deleting
    /// one moves its events to Other.
    void categoryCreated(Event::Category *c);
    void categoryChanged(Event::Category *c);
    void categoryDeleted(Event::Category *c);

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void build();
    void buildSide();
    void buildToolbar(QVBoxLayout *col);
    void buildEditor();
    void applyWidth();
    void refreshToolbar();
    void refreshSide();
    void shift(int direction);
    void activate(const Item &item);
    void toggleDone(const Item &item);
    /// Dragged to another day and, on the grid, to other hours (-1 keeps them).
    void moveItem(const Item &item, const QDate &day, qreal start, qreal end);
    /// Opens the form. A null @p e means a new one starting on @p day at @p hour.
    void openEditor(Event *e, const QDate &day, qreal hour);
    void saveEditor();
    void setEditorKind(int kind);
    void refreshEditorChoices();
    /// Rebuilds the form's category buttons if the list changed (new, renamed,
    /// recoloured, or deleted on another machine).
    void rebuildCatButtons();
    /// Creates (null @p c) or edits a custom category. Created from the form, it
    /// becomes the chosen one for the event being written.
    void openCategoryEditor(Event::Category *c, QWidget *anchor, bool pickForForm);

    /// What was copied, as it was then: pasting still works if the original is
    /// edited or deleted afterwards.
    struct Clip {
        Item::Source source = Item::FromEvent;
        QJsonObject data;     ///< Event::toJson() or Note::toJson(). Empty: nothing.
        QDate day;            ///< The occurrence copied.
        QString title;
        QString text;         ///< What went to the system clipboard.
    };
    static Clip clipOf(const Item &item);
    /// Whether the event or note behind @p item still exists (a sync may have
    /// removed it while a menu was open).
    bool alive(const Item &item) const;
    Spot spotAt(const QPoint &globalPos) const;
    /// The block with keyboard focus on the grid, or the first copyable item of
    /// the selected day in the month.
    bool keyboardItem(Item &out) const;
    void copyItem(const Item &item);
    /// A copy of @p clip on @p day, at @p hour if given (keeping its length).
    void pasteClip(const Clip &clip, const QDate &day, qreal hour);
    void duplicateItem(const Item &item);
    /// Turns the form of an existing event into a new one with the same fields.
    void duplicateInForm();
    void openContextMenu(const Spot &spot, const QPoint &globalPos);

    Theme m_theme;
    Clip m_clip;
    const QList<Event *> *m_events = nullptr;
    const QList<Note *> *m_notes = nullptr;
    const QList<Birthday *> *m_birthdays = nullptr;
    const QList<Event::Category *> *m_categories = nullptr;
    View m_view = Month;
    QDate m_cursor = QDate::currentDate();
    QSet<QString> m_hidden;
    bool m_wide = true;

    QWidget *m_side = nullptr;
    MiniMonth *m_mini = nullptr;
    QVBoxLayout *m_todayList = nullptr;
    QVBoxLayout *m_catList = nullptr;

    QHBoxLayout *m_bar1 = nullptr;
    QHBoxLayout *m_bar2 = nullptr;
    QWidget *m_bar2Host = nullptr;
    QWidget *m_views = nullptr;          ///< Day / Week / Month
    QList<QToolButton *> m_viewButtons;
    QLabel *m_range = nullptr;
    QToolButton *m_todayBtn = nullptr;
    QToolButton *m_newBtn = nullptr;

    QStackedWidget *m_stack = nullptr;
    QWidget *m_gridPage = nullptr;
    QWidget *m_weekHead = nullptr;
    QScrollArea *m_gridScroll = nullptr;
    TimeGrid *m_grid = nullptr;
    MonthBoard *m_month = nullptr;
    bool m_scrolledOnce = false;

    QWidget *m_editor = nullptr;
    QLabel *m_editorTitle = nullptr;
    QLineEdit *m_fTitle = nullptr;
    QLineEdit *m_fDate = nullptr;
    QLineEdit *m_fStart = nullptr;
    QLineEdit *m_fEnd = nullptr;
    QWidget *m_fEndBox = nullptr;
    QWidget *m_fStartBox = nullptr;
    QLineEdit *m_fLastDay = nullptr;     ///< Last day of an all-day event.
    QWidget *m_fLastDayBox = nullptr;
    QToolButton *m_fAllDayBtn = nullptr;
    bool m_fAllDay = false;
    QTextEdit *m_fDesc = nullptr;
    QList<QToolButton *> m_alertButtons;   ///< "minutes" property: -1 means no alert.
    QLabel *m_fError = nullptr;
    QToolButton *m_fDelete = nullptr;
    QToolButton *m_fDuplicate = nullptr;
    QList<QToolButton *> m_kindButtons;
    QList<QToolButton *> m_repeatButtons;
    QList<QToolButton *> m_catButtons;
    QWidget *m_repeatBox = nullptr;
    QWidget *m_catBox = nullptr;
    QString m_catSignature;       ///< Which list m_catBox's buttons were built from.
    Event *m_editing = nullptr;   ///< Null means a new one.
    int m_fKind = 0;              ///< 0 event, 1 task, 2 reminder
    int m_fRepeat = 0;
    QString m_fCategory = "work";
    int m_fAlert = -1;            ///< Lead time in minutes, -1 for none.
};
