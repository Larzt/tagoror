#pragma once

#include <QList>
#include <QWidget>

#include "core/timer.hpp"
#include "ui/theme.hpp"

class QLabel;
class QLineEdit;
class QVBoxLayout;

/// The timers page (5.0 layout): the timer that matters on top with its
/// ring — the one picked, else the one running or ringing, else a paused one —
/// then every other timer as a row under *Guardados*, then *Rápido* chips that
/// start one at once. The creation form opens from the header's "+".
///
/// Like the other pages it stores nothing and changes no state itself: it
/// draws the Store's list and asks Panel for every change. refresh() rebuilds
/// the page, tick() only rewrites numbers and rings; tick() runs every half
/// second while something counts, and rebuilding at that rate would take the
/// focus and half-typed text from the new-timer form.
class TimerView : public QWidget {
    Q_OBJECT

public:
    explicit TimerView(const Theme &theme, QWidget *parent = nullptr);

    void setSource(const QList<Timer *> *timers);
    void setTheme(const Theme &theme);
    void refresh();
    /// The creation form is not rebuilt by refresh() (see m_creator), so a
    /// language change rebuilds it separately.
    void retranslate();
    void tick();

    /// Puts one on top, in the large view.
    void focusTimer(Timer *t);
    /// The header's "+": shows or hides the creation form.
    void toggleCreator();

signals:
    void createRequested(const QString &name, qint64 ms);
    void toggleRequested(Timer *t);    ///< Start or pause.
    void resetRequested(Timer *t);
    void removeRequested(Timer *t);
    void plusRequested(Timer *t);      ///< "+1 min".

private:
    void addSection(const QString &title);
    void addRow(Timer *t);
    void addHero(Timer *t);
    void addQuick();
    void addCreator();
    void openRowMenu(Timer *t, const QPoint &globalPos);
    QString subtitle(const Timer *t) const;
    /// "de 05:00 · termina 17:46" under the large one.
    QString heroSubtitle(const Timer *t) const;
    /// Appends before the trailing stretch.
    void append(QWidget *w);

    Theme m_theme;
    const QList<Timer *> *m_timers = nullptr;
    Timer *m_focus = nullptr;
    QVBoxLayout *m_layout = nullptr;   ///< Content plus the trailing stretch.

    /// What tick() rewrites without rebuilding anything.
    struct Live {
        Timer *timer;
        QLabel *time;
        QWidget *ring;
        QLabel *sub = nullptr;   ///< Its subtitle, which counts too ("termina").
        bool hero = false;
    };
    QList<Live> m_live;

    /// Survives refresh(): the user types in it while other timers change state.
    QWidget *m_creator = nullptr;
    bool m_creatorOpen = false;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_h = nullptr;
    QLineEdit *m_m = nullptr;
    QLineEdit *m_s = nullptr;
};
