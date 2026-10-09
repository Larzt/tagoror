#pragma once

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <functional>

class QScreen;
class QStackedWidget;
class QToolButton;
class QWidget;
class Store;

/// Which page is open and how big the window is for it. Split out of Panel,
/// which did this alongside everything else: the two stacks (open panel or
/// dock; note list or another page), size per page, the planner's widening,
/// the stretch to a page's minimum height, folding to the dock and back, and
/// where the window may be placed at all (see *Window behavior* in CLAUDE.md).
///
/// It owns no widget: Panel builds the shell, the dock and the pages and hands
/// them over, and is told through the callbacks when a page changes.
class Navigator : public QObject {
public:
    /// A body page other than the list, with what leads to it.
    struct Page {
        QWidget *page;
        QToolButton *button;   ///< May be null: a page with no button of its own.
        QString key;           ///< Stored in notes.json (pageSizes): never translated.
        const char *name;      ///< Spanish; translated when shown.
    };

    static constexpr int kShadowMargin = 22;   ///< Room around the frame for the shadow.
    static constexpr int kShellMinWidth = 300;
    static constexpr int kShellMinHeight = 340;

    Navigator(QWidget *window, Store *store, QObject *parent = nullptr);

    /// @p outer swaps @p shell (the open panel) and @p dock; @p body swaps
    /// @p list and the pages added with addPage().
    void setStacks(QStackedWidget *outer, QWidget *shell, QWidget *dock,
                   QStackedWidget *body, QWidget *list);
    void addPage(QWidget *page, QToolButton *button, const QString &key, const char *name);
    /// The page that widens the window on entering (the planner).
    void setWidePage(QWidget *page, int preferredWidth) {
        m_widePage = page;
        m_wideWidth = preferredWidth;
    }
    /// Widgets that only make sense over the list (area tabs, search row).
    void setListOnly(const QList<QWidget *> &widgets) { m_listOnly = widgets; }
    /// Once everything is added: the list in front, and the hidden pages'
    /// minimums out of the way from birth (the planner needs more height than
    /// the list and would impose it on the window).
    void initBody();

    const QList<Page> &pages() const { return m_pages; }
    QWidget *current() const;
    QWidget *list() const { return m_list; }
    bool onList() const { return current() == m_list; }
    bool folded() const;
    QString keyOf(const QWidget *page) const;   ///< Empty for the list.

    /// The whole page switch, with or without size per page.
    void open(QWidget *page);
    void collapse();
    void expand();

    /// @name Window geometry
    /// @{
    int shadowMargin() const;
    bool sizePerPage() const;
    /// Size per page switched on or off from settings.
    void setSizePerPage(bool on);
    /// App mode drops or brings back the shadow margin: the window and the
    /// remembered sizes change by @p delta so the panel inside keeps its size.
    void shiftSizes(int delta);
    /// Where the window manager lets the window be, which is not the whole
    /// screen (see the definition). Without @p sc the window's own screen.
    QRect placementArea(const QScreen *sc = nullptr) const;
    /// The corner the window grows or shrinks from: it keeps the top-left unless
    /// growing from there would leave the placement area.
    QPoint anchoredTopLeft(const QRect &before, const QSize &after) const;
    void keepOnScreen();
    /// Puts the window back where it was saved.
    void restoreWindowPos();
    /// The first map: the window manager placed it, so it is placed again once.
    void mapped();
    bool hasMapped() const { return m_posRestored; }
    /// The minimum height the visible page's layout needs, and applying it.
    int shellMinimumHeight() const;
    void syncShellMinimum();
    /// Initial size at startup, and the open panel shown.
    void start(const QSize &saved);
    /// Copies the window geometry into the preferences, before every save.
    void syncPrefs();
    /// @}

    /// After a page was shown (and on start), for the title, the footer and the
    /// page buttons.
    std::function<void()> pageChanged;

private:
    void showPage(QWidget *page);       ///< Outer stack: shell or dock.
    void showBodyPage(QWidget *page);   ///< Inner stack, without resizing.
    void rememberPageSize(QWidget *page);
    /// @return false if the page has no stored size yet.
    bool applyPageSize(QWidget *page);
    void syncPageHome(QWidget *from);
    void enterWide();
    void leaveWide();

    QWidget *m_window;
    Store *m_store;
    QStackedWidget *m_outer = nullptr;
    QWidget *m_shell = nullptr;
    QWidget *m_dock = nullptr;
    QStackedWidget *m_body = nullptr;
    QWidget *m_list = nullptr;
    QList<Page> m_pages;
    QList<QWidget *> m_listOnly;

    QWidget *m_widePage = nullptr;
    int m_wideWidth = 0;

    QSize m_expandedSize;          ///< Restored when unfolding (and saved).
    QSize m_listSize;              ///< The list's, with size per page.
    QPoint m_dockOffset;           ///< The point of the panel the dock goes in and out through.
    bool m_posRestored = false;    ///< The saved position is restored only on map.
    /// The geometry before the window was stretched to fit a page's minimum, and
    /// the one it was left at. Leaving restores the first, but only if the second
    /// is still the current geometry: once the user resizes, the size is theirs.
    QRect m_grownFrom;
    QRect m_grownTo;
    /// The same for the planner's width (see enterWide()).
    QRect m_narrowGeom;
    QRect m_wideGeom;
    /// With size per page, every page is placed from the list's geometry
    /// (m_pageHome), never from the page being left: otherwise a large page that
    /// opened leftwards or upwards dragged the corner along. m_pagePlaced is
    /// where the open page was placed, to tell whether the user moved it.
    QRect m_pageHome;
    QRect m_pagePlaced;
};
