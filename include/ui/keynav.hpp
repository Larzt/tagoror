#pragma once

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QWidget>
#include <functional>

/// @file
/// Keyboard navigation for the hand-made clickables (menu rows, settings
/// rows, links, date chip, colour dots, switches), which Qt does not make
/// reachable on its own. They get Tab focus (not click focus: a ring after a
/// mouse click is noise) and activate on Enter or Space.
namespace keynav {

class Activator : public QObject {
public:
    Activator(QWidget *target, std::function<void()> activate)
        : QObject(target), m_activate(std::move(activate)) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        auto *w = static_cast<QWidget *>(watched);
        switch (event->type()) {
            case QEvent::KeyPress: {
                const int key = static_cast<QKeyEvent *>(event)->key();
                if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) &&
                    m_activate) {
                    m_activate();
                    return true;
                }
                break;
            }
            // Painted widgets do not repaint on focus changes by themselves.
            case QEvent::FocusIn:
            case QEvent::FocusOut:
                w->update();
                break;
            default:
                break;
        }
        return false;
    }

private:
    std::function<void()> m_activate;
};

/// Makes @p w reachable with Tab and activatable with Enter/Space.
inline void activatable(QWidget *w, std::function<void()> activate) {
    w->setFocusPolicy(Qt::TabFocus);
    w->installEventFilter(new Activator(w, std::move(activate)));
}

/// Whether to paint the focus ring: only when focus arrived by keyboard (the
/// web's :focus-visible). When a focused widget is hidden, Qt hands focus to
/// the next in the chain, and a ring there that nobody asked for looks stuck.
inline bool showsFocus(const QWidget *w) {
    return w->hasFocus() && w->property("kbfocus").toBool();
}

/// Application-wide filter with two jobs:
///  - record in every widget whether focus arrived by keyboard (the "kbfocus"
///    property the stylesheet's focus rules and showsFocus() read);
///  - make Enter click the focused button (QAbstractButton only answers
///    Space; QPushButton already handles Enter and is left alone).
class ButtonEnter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        // Not text fields: their focus border always shows, and repolishing a
        // MarkdownEdit on every focus change would re-render it.
        if (event->type() == QEvent::FocusIn && watched->isWidgetType() &&
            !qobject_cast<QLineEdit *>(watched) && !qobject_cast<QAbstractScrollArea *>(watched)) {
            auto *w = static_cast<QWidget *>(watched);
            const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
            // Menu arrows and shortcuts are keyboard too.
            const bool keyboard = reason == Qt::TabFocusReason ||
                                  reason == Qt::BacktabFocusReason ||
                                  reason == Qt::ShortcutFocusReason;
            if (w->property("kbfocus").toBool() != keyboard) {
                w->setProperty("kbfocus", keyboard);
                // A dynamic property does not repaint by itself.
                w->style()->unpolish(w);
                w->style()->polish(w);
                w->update();
            }
            return false;
        }
        if (event->type() != QEvent::KeyPress) return false;
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key != Qt::Key_Return && key != Qt::Key_Enter) return false;
        auto *button = qobject_cast<QAbstractButton *>(watched);
        if (!button || qobject_cast<QPushButton *>(watched) || !button->hasFocus() ||
            !button->isEnabled())
            return false;
        button->animateClick();
        return true;
    }
};

inline void installButtonEnter() {
    static ButtonEnter *filter = nullptr;
    if (filter) return;
    filter = new ButtonEnter(qApp);
    qApp->installEventFilter(filter);
}

}  // namespace keynav
