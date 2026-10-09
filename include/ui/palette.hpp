#pragma once

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QLineEdit;
class QScrollArea;

namespace palette {
class CommandList;
}

/// The command palette (Ctrl+K): go to any page, create any kind of note,
/// switch area, from anywhere and without the mouse.
///
/// It is drawn *inside* the panel, over a dimmed copy of it, rather than as a
/// Popup window: it is the panel's own search box for actions, and a separate
/// window would stack on its own over the desktop. Panel supplies the commands
/// fresh on every open (the areas change), each with its action.
class CommandPalette : public QWidget {
public:
    struct Command {
        QString group;      ///< Heading it sits under: "Ir a", "Crear", "Áreas".
        QString icon;       ///< paintIcon() kind.
        QString label;
        QString hint;       ///< Its shortcut, shown on the right.
        std::function<void()> run;
    };

    /// @p host is the widget it covers (the shell).
    CommandPalette(const Theme *theme, QWidget *host);

    void open(const QList<Command> &commands);
    void close();
    bool isOpen() const { return isVisible(); }

    /// Folded to lower case and stripped of accents, so "cumple" finds
    /// "Cumpleaños" and "recor" "Nuevo recordatorio".
    static QString fold(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void filter();
    void runSelected();
    void place();

    const Theme *m_theme;
    QWidget *m_dialog = nullptr;
    QLineEdit *m_field = nullptr;
    QScrollArea *m_scroll = nullptr;
    palette::CommandList *m_list = nullptr;
    QList<Command> m_all;
    QList<Command> m_shown;     ///< What the filter left, in the list's order.
    QPointer<QWidget> m_back;   ///< Where the focus was, to give it back on close.
};
