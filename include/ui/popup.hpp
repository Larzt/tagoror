#pragma once

#include <climits>

#include <QColor>
#include <QList>
#include <QStringList>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QFrame;
class QVBoxLayout;

/// The app's own menu, used everywhere instead of QMenu.
///
/// QMenu is a native window that does not inherit the panel's translucency,
/// so against the rounded frame it rendered opaque with square corners. Popup
/// rebuilds the panel's look (translucent frame plus shadow). Rows are plain
/// widgets holding a std::function, so adding one needs no moc.
class Popup : public QWidget {
    Q_OBJECT

public:
    /// Menus render opaque even when the panel is translucent: a see-through menu
    /// over the cards is unreadable. Translucent is opt-in.
    enum Surface { Opaque, Translucent };

    explicit Popup(const Theme &theme, QWidget *anchor = nullptr, Surface surface = Opaque);

    void addHeader(const QString &text);
    void addItem(const QString &iconKind, const QString &title,
                 const QString &subtitle = QString(),
                 std::function<void()> action = nullptr);
    void addEditor(const QString &placeholder, const QString &text,
                   std::function<void(const QString &)> commit);
    /// Several fields confirmed together. Two addEditor() calls cannot do this:
    /// each field would close the popup on its own Enter.
    void addFields(const QStringList &placeholders, const QStringList &values,
                   std::function<void(const QStringList &)> commit);
    /// Small buttons in rows of two or three. A list of hours as menu rows is
    /// taller than the calendar that opens it.
    void addChips(const QStringList &labels, const QList<bool> &muted,
                  const QString &mutedTip, std::function<void(int)> action);
    /// Chips with one chosen: a setting with state (how often), unlike
    /// addChips(), which is a list of actions. The chosen one is marked.
    void addChoice(const QStringList &labels, int chosen, std::function<void(int)> action);
    /// Same with icons instead of text, all in one row (the area glyphs).
    void addIconChoice(const QStringList &iconKinds, const QStringList &tips, int chosen,
                       std::function<void(int)> action);
    /// A loose paragraph, for what a menu must explain before offering anything.
    void addText(const QString &text);
    /// A name and a colour at once, for planner categories. Enter confirms with
    /// the marked colour; clicking a colour confirms with that one and the text.
    void addNameColor(const QString &placeholder, const QString &name,
                      const QList<QColor> &colors, int chosen,
                      std::function<void(const QString &, int)> commit);
    void addSeparator();

    /// Places the popup under @p anchor, clamped to the screen.
    void showUnder(QWidget *anchor);
    /// Like showUnder(), but never moved up over @p anchor: near the bottom edge
    /// the clamp used to push the menu over the calendar that opened it.
    void showBelow(QWidget *anchor);
    void showAt(const QPoint &globalPos);

protected:
    /// Up and Down walk the rows, like any menu; Tab too.
    void keyPressEvent(QKeyEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    /// Runs @p action after closing: "delete" destroys the card that opened the
    /// popup, so it cannot run while the popup is alive.
    void run(const std::function<void()> &action);
    /// @p minY limits the upward correction: the popup rather overflows the
    /// screen bottom than covers the widget that opened it.
    void place(const QPoint &globalTopLeft, int minY = INT_MIN);

    Theme m_theme;
    QFrame *m_shell = nullptr;
    QVBoxLayout *m_col = nullptr;
};
