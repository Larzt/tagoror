#pragma once

#include <QPointer>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QLabel;
class QVBoxLayout;

/// A dialog drawn inside the panel, over a dimmed copy of it, like the command
/// palette: Seguimiento's session, project and export forms (1.1). A separate
/// window would stack on its own over the desktop, and a Popup closes on the
/// first click elsewhere, which a form with several fields cannot afford.
///
/// It owns nothing but its frame: the form is built by whoever opens it and
/// handed over with setContent(). Escape or a click on the dimmed part closes
/// it, and it deletes itself once closed.
class Overlay : public QWidget {
public:
    /// @p host is the widget it covers (the shell).
    Overlay(const Theme *theme, QWidget *host, const QString &title);

    void setContent(QWidget *content);
    /// Widest the dialog gets; on a narrow panel it takes the panel's width.
    void setMaxWidth(int px) { m_maxWidth = px; }
    void open(QWidget *focus = nullptr);
    /// The form grew or shrank (a warning appeared): fit the dialog to it.
    void relayout() { if (isVisible()) place(); }
    void close();

    /// Called once it has closed, however that happened.
    std::function<void()> closed;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();

    QWidget *m_dialog = nullptr;
    QVBoxLayout *m_col = nullptr;
    QWidget *m_content = nullptr;
    int m_maxWidth = 380;
    QPointer<QWidget> m_back;   ///< Where the focus was, to give it back on close.
};
