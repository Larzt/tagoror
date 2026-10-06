#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <QWidget>
#include <functional>

#include "ui/theme.hpp"

class QLineEdit;

/// The strip of workspace-area tabs between the header and the list.
///
/// Painted by hand rather than a QTabBar: it draws each area's glyph, the red
/// dot of areas with something ringing and a "+N" for the ones that do not
/// fit, and never scrolls sideways. The active tab is always shown; hidden
/// ones go behind "+N", which inherits their red dot. Talks through
/// std::function rather than signals, so no moc.
class AreaTabs : public QWidget {
public:
    struct Tab {
        QString id;
        QString name;
        QString glyph;
        bool ringing = false;
    };

    /// @p theme points at the panel's own, so an accent change only needs an
    /// update().
    explicit AreaTabs(const Theme *theme, QWidget *parent = nullptr);

    void setTabs(const QList<Tab> &tabs, const QString &active);
    QString active() const { return m_active; }

    /// Edits that tab's name in place. Enter or losing focus confirms, Escape
    /// cancels.
    void beginRename(const QString &id);

    /// Area under a screen point, or empty; used to drop a card on a tab.
    /// setDropTarget() highlights it during the drag.
    QString areaAt(const QPoint &globalPos) const;
    void setDropTarget(const QString &id);

    /// Where to open a tab's (or "+N"'s) menu: its bottom-left corner, in screen
    /// coordinates.
    QPoint menuPoint(const QString &id) const;

    std::function<void(const QString &)> activated;
    std::function<void(const QString &, const QPoint &)> menuRequested;
    std::function<void(const QPoint &)> overflowRequested;
    std::function<void()> addRequested;
    std::function<void(const QString &, const QString &)> renamed;
    std::function<void(const QString &)> deleteRequested;
    std::function<void(const QString &, int)> moveRequested;

    QSize sizeHint() const override;
    /// Small on purpose: the strip must not demand the width of its names or it
    /// would widen the whole panel (see *Card widths*).
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// What occupies each slot of the strip; @c index is the position in m_tabs.
    struct Slot {
        int index;
        QRect rect;
        QString text;   ///< The name, already elided if needed.
    };
    void relayout();
    int tabWidth(const Tab &t, QString *elided) const;
    int indexOf(const QString &id) const;
    /// @return -2 for "+", -3 for "+N", -1 for nothing, else the tab index.
    int hitTest(const QPoint &pos) const;
    void activate(int index);
    void finishRename(bool commit);

    const Theme *m_theme;
    QList<Tab> m_tabs;
    QString m_active;
    QList<Slot> m_slots;
    QRect m_overflowRect;
    QRect m_addRect;
    int m_hidden = 0;
    bool m_hiddenRinging = false;
    int m_hover = -1;
    QString m_drop;
    QLineEdit *m_editor = nullptr;
    QString m_editing;
};
