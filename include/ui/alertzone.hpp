#pragma once

#include <QList>
#include <QString>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QVBoxLayout;

/// The one place notices appear (5.0). It replaced three strips that came and
/// went under the header — the alarm of a timer or event, the update, the
/// missing data folder — and made the list jump every time one did.
///
/// It floats over the bottom of the page, just above the navigation, instead
/// of taking height in the layout; the list gains bottom padding so its last
/// card can still be scrolled clear of it. Only the most urgent notice shows
/// (alarm › folder › sync › update); "1 de N" unfolds the rest above it.
///
/// It knows nothing about notes or timers: Panel builds the notices with their
/// actions, the same way Popup rows hold a std::function.
class AlertZone : public QWidget {
public:
    struct Notice {
        enum Kind { Alarm, Warn, Info };
        Kind kind = Info;
        QString icon;     ///< paintIcon() kind.
        QString title;
        QString sub;
        QString action;   ///< The main button: Detener, Reintentar, Ver…
        std::function<void()> act;
        QString second;   ///< Alarms only: Posponer.
        std::function<void()> secondAct;
    };

    explicit AlertZone(const Theme *theme, QWidget *parent = nullptr);

    /// Rebuilds only when something visible changed: it is called from the
    /// heartbeat, and a rebuild under the pointer would eat a click.
    void setNotices(const QList<Notice> &notices);
    bool hasNotices() const { return !m_notices.isEmpty(); }

    /// The height it needs at @p width; Panel places it.
    int heightFor(int width) const;

    /// Called when the zone appears, disappears or changes height.
    std::function<void()> changed;

private:
    void rebuild();
    QWidget *buildTop(const Notice &n);
    QWidget *buildList();

    const Theme *m_theme;
    QList<Notice> m_notices;
    QString m_signature;
    bool m_expanded = false;
    QVBoxLayout *m_col = nullptr;
};
