#pragma once

#include <QList>
#include <QWidget>

#include "core/birthday.hpp"
#include "ui/theme.hpp"

class QVBoxLayout;

/// The birthdays page.
///
/// On top, the next birthday due on a highlighted card with what can be done
/// about it (greet, only on the day; set or clear an alarm). It is the
/// soonest, not today's, or the page would have nothing highlighted most of
/// the year. Below, everyone else split by month, ordered by time left or by
/// calendar as the user picks.
///
/// Everything scrolls, highlighted card included: outside the scroll area the
/// page's minimum height depended on how many people share the soonest date,
/// and the window grew with it. Like the other pages it stores nothing.
class BirthdayView : public QWidget {
    Q_OBJECT

public:
    explicit BirthdayView(const Theme &theme, QWidget *parent = nullptr);

    /// The list is owned by the Store and outlives this view.
    void setSource(const QList<Birthday *> *list);
    void setTheme(const Theme &theme);
    /// Order of the list. Stored in the Store's prefs; the view is told at
    /// construction and reports changes through orderChanged().
    void setByMonth(bool on);
    bool byMonth() const { return m_byMonth; }

    /// Re-reads the list: highlighted card and rows.
    void refresh();

    /// Re-translates after a language change. Just refresh(): the page is rebuilt
    /// whole every time and keeps no fixed text.
    void retranslate() { refresh(); }

signals:
    void addRequested(QWidget *anchor);                    ///< "Add birthday".
    void editRequested(Birthday *b, QWidget *anchor);      ///< A row's menu.
    void remindRequested(Birthday *b, QWidget *anchor);    ///< Pick the alarm time.
    void greetToggled(Birthday *b);                        ///< Greeted / not greeted.
    void dismissRequested(Birthday *b);                    ///< Silence a ringing alarm.
    void orderChanged(bool byMonth);                       ///< So it gets saved.

private:
    QWidget *buildHighlightCard(Birthday *b);
    QWidget *buildHeader(int listed);
    QWidget *buildMonthHeader(int month);
    QWidget *buildRow(Birthday *b);
    QWidget *buildAddRow();
    /// All valid birthdays by time left. The first ones (tied for soonest) are
    /// the highlighted ones.
    QList<Birthday *> sorted() const;
    QList<Birthday *> byCalendar() const;

    Theme m_theme;
    const QList<Birthday *> *m_list = nullptr;
    bool m_byMonth = false;
    QVBoxLayout *m_layout = nullptr;   ///< Content plus the trailing stretch.
};
