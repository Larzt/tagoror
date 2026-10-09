#pragma once

#include <QAbstractButton>
#include <QList>
#include <QString>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QBoxLayout;

/// The fixed page navigation of 5.0: Notas · Planner · Timers · Cumpleaños,
/// always visible, with the open one marked. Before it every page was a
/// header button that toggled back to the notes, so nothing said "you are
/// here".
///
/// A bar along the bottom (52px) while the panel is narrow, where height is
/// cheaper than width; a rail down the left side (68px) from kRailFrom up,
/// which is where the planner lives. Panel moves it between the two slots.
/// Painted items, no Q_OBJECT: callbacks, like Popup's rows.
class NavBar : public QWidget {
public:
    enum Layout { Bar, Rail };
    static constexpr int kBarHeight = 52;
    static constexpr int kRailWidth = 68;
    static constexpr int kRailFrom = 480;   ///< Shell width at which it becomes a rail.

    struct Item {
        QWidget *page;      ///< What it opens; the list for Notas.
        QString icon;       ///< paintIcon() kind.
        const char *name;   ///< Spanish; translated when painted.
        QString shortcut;   ///< Shown in the tooltip.
    };

    explicit NavBar(const Theme *theme, QWidget *parent = nullptr);

    void setItems(const QList<Item> &items);
    void setLayoutMode(Layout layout);
    Layout layoutMode() const { return m_layout; }

    /// The open page; null when it is none of them (settings).
    void setActive(QWidget *page);
    /// Replaces that item's label with a countdown while it is not empty.
    void setCountdown(QWidget *page, const QString &text);
    /// Red dot: something on that page is ringing and it is not open.
    void setDot(QWidget *page, bool on);
    /// Re-reads the labels and tooltips in the current language.
    void retranslate();

    /// The item's widget, to park the focus on when its page is left.
    QWidget *buttonFor(QWidget *page) const;

    std::function<void(QWidget *)> activated;

private:
    class NavItem;
    QList<NavItem *> m_items;
    QBoxLayout *m_box = nullptr;
    const Theme *m_theme;
    Layout m_layout = Bar;
};
