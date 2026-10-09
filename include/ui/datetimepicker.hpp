#pragma once

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QLabel;
class QScrollArea;
class QToolButton;

namespace picker {
class DayGrid;
class TimeList;
}

/// Date and time chooser for a reminder, or time alone for a birthday's alert:
/// a row of quick picks, a month grid and a column of times in 5 or 15 minute
/// steps, confirmed with *Guardar*. It replaced the "A mano · dd/MM HH:mm"
/// field, which only worked for whoever knew the format.
///
/// It lives inside a Popup (Popup::addDateTime / addTime), which is what closes
/// it: the picker only reports through its callbacks.
class DateTimePicker : public QWidget {
public:
    enum Mode { DateAndTime, TimeOnly };

    /// Quick picks: a label and the instant it stands for. Picking one commits
    /// straight away, as the old preset rows did.
    using Quick = QList<QPair<QString, QDateTime>>;

    DateTimePicker(const Theme &theme, Mode mode, const QDateTime &initial,
                   const Quick &quick = {}, QWidget *parent = nullptr);

    std::function<void(const QDateTime &)> onCommit;
    std::function<void()> onCancel;

    /// Where the keyboard starts: the grid, or the time list in TimeOnly mode.
    QWidget *initialFocus() const;

    QDateTime value() const { return QDateTime(m_date, m_time); }

private:
    void setDate(const QDate &d);
    void setTime(const QTime &t);
    void setStep(int minutes);
    /// The summary line, the step track and whether *Guardar* is allowed.
    void refresh();
    void commit();

    const Theme &m_theme;
    Mode m_mode;
    QDate m_date;
    QTime m_time;

    picker::DayGrid *m_grid = nullptr;
    picker::TimeList *m_times = nullptr;
    QScrollArea *m_timeScroll = nullptr;
    QLabel *m_summary = nullptr;
    QToolButton *m_step5 = nullptr;
    QToolButton *m_step15 = nullptr;
    QToolButton *m_save = nullptr;
};
