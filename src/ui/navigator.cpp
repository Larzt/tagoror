#include "ui/navigator.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QLayout>
#include <QScreen>
#include <QStackedWidget>
#include <QWidget>

#include "core/store.hpp"
#include "ui/workarea.hpp"

namespace {

/// One axis of the anchoring. The window keeps its top-left corner; only when
/// growing from there would not fit is the opposite edge anchored, so it
/// opens backwards (leftwards against the right edge, upwards against the
/// bottom). If neither fits it is clamped.
///
/// "Do not move" being the first choice is what makes a fold/unfold round
/// trip exact: if the panel fitted there, so does the dock coming out of its
/// corner, and vice versa. @p end is the first point already outside.
int anchorAxis(int pos, int len, int newLen, int lo, int end) {
    const int keepStart = pos;
    const int keepEnd = pos + len - newLen;
    if (keepStart >= lo && keepStart + newLen <= end) return keepStart;
    if (keepEnd >= lo && keepEnd + newLen <= end) return keepEnd;
    return qBound(lo, keepStart, qMax(lo, end - newLen));
}

/// Pushes a rectangle inside the area where it may be placed.
QPoint clampInto(QPoint pos, const QSize &size, const QRect &area) {
    if (!area.isValid()) return pos;
    return {qBound(area.left(), pos.x(), qMax(area.left(), area.right() + 1 - size.width())),
            qBound(area.top(), pos.y(), qMax(area.top(), area.bottom() + 1 - size.height()))};
}

}  // namespace

Navigator::Navigator(QWidget *window, Store *store, QObject *parent)
    : QObject(parent), m_window(window), m_store(store) {}

void Navigator::setStacks(QStackedWidget *outer, QWidget *shell, QWidget *dock,
                          QStackedWidget *body, QWidget *list) {
    m_outer = outer;
    m_shell = shell;
    m_dock = dock;
    m_body = body;
    m_list = list;
}

void Navigator::addPage(QWidget *page, QWidget *button, const QString &key,
                        const char *name) {
    m_pages.append({page, button, key, name});
}

QWidget *Navigator::current() const { return m_body ? m_body->currentWidget() : nullptr; }

bool Navigator::folded() const { return m_outer && m_outer->currentWidget() == m_dock; }

QString Navigator::keyOf(const QWidget *page) const {
    for (const Page &p : m_pages)
        if (p.page == page) return p.key;
    return {};
}

int Navigator::shadowMargin() const { return m_store->prefs().appMode ? 0 : kShadowMargin; }

/// Widget only: an ordinary window keeps the size and place the user gives it,
/// whatever page is open. The preference is kept for when it goes back.
bool Navigator::sizePerPage() const {
    return !m_store->prefs().appMode && m_store->prefs().sizePerPage;
}

void Navigator::setSizePerPage(bool on) {
    if (on) {
        // The list's size comes from the old rule (without the planner's widening or
        // the stretch), and the open page keeps its current one.
        syncPrefs();
        m_listSize = m_store->prefs().windowSize;
        m_store->prefs().sizePerPage = true;
        rememberPageSize(current());
        m_grownFrom = m_grownTo = m_narrowGeom = m_wideGeom = QRect();
    } else {
        // The recorded sizes are kept: turning it back on restores them.
        m_store->prefs().sizePerPage = false;
    }
}

void Navigator::shiftSizes(int delta) {
    m_window->resize(m_window->size() + QSize(delta, delta));
    m_expandedSize += QSize(delta, delta);
    m_listSize += QSize(delta, delta);
    for (QSize &s : m_store->prefs().pageSizes) s += QSize(delta, delta);
    m_grownFrom = m_grownTo = m_narrowGeom = m_wideGeom = QRect();
    // Size per page is off in app mode, so the window may have moved since the
    // list's corner was recorded: back in the widget, it starts again from here.
    m_pageHome = m_pagePlaced = QRect();
    keepOnScreen();
}

void Navigator::start(const QSize &saved) {
    m_expandedSize = saved;
    if (!m_expandedSize.isValid())
        m_expandedSize = QSize(352 + shadowMargin() * 2, 560);
    m_listSize = m_expandedSize;
    m_window->resize(m_expandedSize);
    showPage(m_shell);
}

void Navigator::initBody() {
    for (int i = 0; i < m_body->count(); ++i) {
        QWidget *w = m_body->widget(i);
        const auto policy = (w == m_list) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        w->setSizePolicy(policy, policy);
    }
    m_body->setCurrentWidget(m_list);
    for (QWidget *w : m_listOnly) w->setVisible(true);
}

void Navigator::rememberPageSize(QWidget *page) {
    if (!sizePerPage() || folded() || !page) return;
    // Before mapping, the window has Qt's default size, which nobody chose: a
    // save during startup would record it.
    if (!m_posRestored) return;
    if (page == m_list) m_listSize = m_window->size();
    else if (const QString key = keyOf(page); !key.isEmpty())
        m_store->prefs().pageSizes[key] = m_window->size();
}

/// A page with no stored size opens with the list's, not the previous page's,
/// so a first visit to the planner does not inherit a stretched settings
/// page, and the planner widens from there.
bool Navigator::applyPageSize(QWidget *page) {
    const QSize stored = page == m_list ? m_listSize
                                        : m_store->prefs().pageSizes.value(keyOf(page));
    QSize target = stored.isValid() ? stored : m_listSize;
    if (target.isValid()) {
        // Never below what the page needs (see syncShellMinimum()), never more than
        // fits: a size recorded on another monitor.
        target = target.expandedTo(m_window->minimumSize());
        if (const QRect area = placementArea(); area.isValid()) target = target.boundedTo(area.size());
        // From the list, not from the previous page: see m_pageHome.
        const QRect from = m_pageHome.isValid() ? m_pageHome : m_window->geometry();
        const QRect want(anchoredTopLeft(from, target), target);
        if (want != m_window->geometry()) {
            m_window->setGeometry(want);
            keepOnScreen();
        }
    }
    m_grownFrom = m_grownTo = QRect();
    m_pagePlaced = m_window->geometry();
    return stored.isValid();
}

void Navigator::syncPageHome(QWidget *from) {
    if (from == m_list || !m_pageHome.isValid() || !m_pagePlaced.isValid()) {
        // From the list, the list is what is there. With nothing recorded (the mode
        // was just switched on), the closest thing: the current corner.
        const QSize list = from != m_list && m_listSize.isValid() ? m_listSize : m_window->size();
        m_pageHome = QRect(m_window->pos(), list);
        return;
    }
    // However far the user dragged the page, the list goes too; untouched, the
    // offset is zero.
    m_pageHome.translate(m_window->pos() - m_pagePlaced.topLeft());
}

void Navigator::open(QWidget *page) {
    QWidget *from = current();
    if (m_window->isMaximized()) {
        // Maximised (app mode), the window's size belongs to the system: no page
        // resizes it, and none records the maximised size as its own.
        showBodyPage(page);
        if (pageChanged) pageChanged();
        return;
    }
    const bool perPage = sizePerPage() && !folded();
    if (perPage) {
        // How the page is left is its size; the widen and stretch notes belong to the
        // single-size mode and are not given back here.
        syncPageHome(from);
        rememberPageSize(from);
        m_grownFrom = m_grownTo = m_narrowGeom = m_wideGeom = QRect();
    } else if (from == m_widePage) {
        // The width goes back first, or the next page would inherit the window
        // widened for the planner.
        leaveWide();
    }

    showBodyPage(page);

    if (perPage) {
        if (!applyPageSize(page) && page == m_widePage) {
            enterWide();
            m_narrowGeom = m_wideGeom = QRect();
            m_pagePlaced = m_window->geometry();
        }
    } else if (page == m_widePage) {
        enterWide();
    }
    if (pageChanged) pageChanged();
}

/// Widens towards where it fits (anchoredTopLeft(): leftwards against the
/// right edge) and records the result so it can be given back.
void Navigator::enterWide() {
    if (folded()) return;
    int target = m_wideWidth + shadowMargin() * 2;
    if (const QRect area = placementArea(); area.isValid()) target = qMin(target, area.width());
    if (m_window->width() >= target) return;

    const QRect before = m_window->geometry();
    const QSize size(target, m_window->height());
    m_window->setGeometry(QRect(anchoredTopLeft(before, size), size));
    keepOnScreen();
    m_narrowGeom = before;
    m_wideGeom = m_window->geometry();
}

/// Only while the window is still what enterWide() left: if the user resized
/// it meanwhile, that size is theirs.
void Navigator::leaveWide() {
    if (m_narrowGeom.isValid() && m_window->geometry() == m_wideGeom) {
        m_window->setGeometry(m_narrowGeom);
        keepOnScreen();
    }
    m_narrowGeom = QRect();
    m_wideGeom = QRect();
}

void Navigator::showPage(QWidget *page) {
    // Hidden pages of a QStackedWidget still count for the size hint; ignoring
    // them lets the window shrink to the dock.
    for (int i = 0; i < m_outer->count(); ++i) {
        QWidget *w = m_outer->widget(i);
        const auto policy = (w == page) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        w->setSizePolicy(policy, policy);
    }
    // The Ignored policy is not enough: an explicit minimumSize still adds to the
    // QStackedLayout's minimum, and the folded panel kept an invisible 300px
    // strip next to the dock that swallowed clicks.
    m_shell->setMinimumWidth(page == m_shell ? kShellMinWidth : 0);
    m_outer->setCurrentWidget(page);
}

/// Same idea one level in: a QStackedWidget takes the largest minimum of all
/// its pages, so the planner would impose its height on the note list.
void Navigator::showBodyPage(QWidget *page) {
    // If the focus is on the page being left, park it on that page's button
    // first. Hiding a focused widget makes Qt pass the focus down the chain, and
    // on the way back to the list that was the first card's title, which then
    // looked like it was being edited. No ring: it did not arrive by Tab.
    QWidget *leaving = m_body->currentWidget();
    QWidget *focus = QApplication::focusWidget();
    if (leaving && leaving != page && focus && leaving->isAncestorOf(focus)) {
        QWidget *to = nullptr;
        for (const Page &p : m_pages)
            if (p.page == leaving || (!to && p.page == page)) to = p.button;
        if (to) to->setFocus(Qt::OtherFocusReason);
        else m_window->setFocus(Qt::OtherFocusReason);
    }

    for (int i = 0; i < m_body->count(); ++i) {
        QWidget *w = m_body->widget(i);
        const auto policy = (w == page) ? QSizePolicy::Preferred : QSizePolicy::Ignored;
        w->setSizePolicy(policy, policy);
    }
    m_body->setCurrentWidget(page);
    for (QWidget *w : m_listOnly) w->setVisible(page == m_list);
    syncShellMinimum();
}

int Navigator::shellMinimumHeight() const {
    QLayout *l = m_shell ? m_shell->layout() : nullptr;
    if (!l) return kShellMinHeight;

    // Recompute inside out, by hand. Hiding a widget invalidates its parent's
    // layout through a *posted* LayoutRequest: without flushing it, minimumSize()
    // answers with the previous state. Invalidating only the outer layout is not
    // enough: it asks the inner one, which keeps its cached value until then.
    if (QWidget *page = current()) page->updateGeometry();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    l->invalidate();
    l->activate();
    return qMax(kShellMinHeight, l->minimumSize().height() + shadowMargin() * 2);
}

/// The window's minimum comes from the layout, never from a constant.
///
/// A setMinimumSize() below what the layout needs shrinks nothing: Qt hands
/// out the height it has and the geometries overlap. The other way round, a
/// page that needs more height grows the window downwards, and upwards only
/// as far as the work area forces it (the same rule as anchoredTopLeft()).
void Navigator::syncShellMinimum() {
    if (!m_shell || !m_outer || folded()) return;

    // setMinimumSize() already stretches the window when short, keeping the
    // top-left corner: it grows downwards, as wanted. What is left is to record
    // that the stretch is ours and pull it back into the work area.
    const QRect before = m_window->geometry();
    const int minH = shellMinimumHeight();
    m_window->setMinimumSize(kShellMinWidth + shadowMargin() * 2, minH);

    if (m_window->height() > before.height()) {
        // Only the first stretch is recorded: two growths in a row must give back the
        // size from before the first, not the one in between.
        if (!m_grownFrom.isValid()) m_grownFrom = before;
        if (const QRect area = placementArea(); area.isValid())
            if (const QPoint p = clampInto(m_window->pos(), m_window->size(), area);
                p != m_window->pos())
                m_window->move(p);
        m_grownTo = m_window->geometry();
        return;
    }

    // It fits with room to spare. If the window is this big because we stretched
    // it, give it back, or it would grow a little every time. The note only holds
    // while the window is still what we left: once the user touches it, the size
    // is theirs.
    if (m_grownFrom.isValid() && m_window->geometry() == m_grownTo) {
        // Not everything can be given back yet (the page still needs more than the
        // starting size): leave it and keep the note, which is still true. Dropping
        // it here let an intermediate sync eat the return trip.
        if (m_grownFrom.height() < minH) return;
        m_window->setGeometry(m_grownFrom);
        keepOnScreen();
    }
    m_grownFrom = QRect();
    m_grownTo = QRect();
}

void Navigator::collapse() {
    // The saved size is the normal one, not widened for the planner, or unfolding
    // would open wide for any page. With size per page the dock comes out of the
    // list, not the open page: if that opened leftwards or upwards, its corner is
    // not the panel's and the dock would land far from where it was left.
    QRect panel = m_window->geometry();
    if (sizePerPage()) {
        syncPageHome(current());
        rememberPageSize(current());
        panel = m_pageHome;
    } else if (current() == m_widePage) {
        leaveWide();
        panel = m_window->geometry();
    }
    m_expandedSize = m_window->size();
    // The stretch note does not survive the dock: the geometry it compared
    // against is the open panel's, which no longer exists.
    m_grownFrom = QRect();
    m_grownTo = QRect();

    m_window->setMinimumSize(0, 0);
    showPage(m_dock);

    // The dock goes back to where the panel came out of, not to its top-left: if
    // it was at the bottom and the panel opened upwards, folding must return it
    // there. With m_dockOffset at zero (no unfold yet) that is the top-left.
    const QSize dock = m_dock->sizeHint() + QSize(shadowMargin() * 2, shadowMargin() * 2);
    const QPoint inside(qBound(0, m_dockOffset.x(), qMax(0, panel.width() - dock.width())),
                        qBound(0, m_dockOffset.y(), qMax(0, panel.height() - dock.height())));
    m_window->setGeometry(QRect(clampInto(panel.topLeft() + inside, dock, placementArea()), dock));
    keepOnScreen();
}

void Navigator::expand() {
    // The dock's geometry before touching anything: setMinimumSize() below
    // already stretches the window, after which this corner is no longer the
    // dock's.
    const QRect dock = m_window->geometry();

    showPage(m_shell);
    m_window->setMinimumSize(kShellMinWidth + shadowMargin() * 2, shellMinimumHeight());

    // A saved size larger than what fits (another monitor or resolution, a desktop
    // panel cutting the work area) is clamped first; otherwise the window manager
    // clamps it and moves the window too.
    QSize target = m_expandedSize;
    // With size per page it opens with the size of the page it was folded on (or
    // the list's, if that has none).
    bool hadPageSize = false;
    if (sizePerPage()) {
        QWidget *cur = current();
        const QSize stored = cur == m_list ? m_listSize
                                           : m_store->prefs().pageSizes.value(keyOf(cur));
        hadPageSize = stored.isValid();
        if (hadPageSize) target = stored;
        else if (m_listSize.isValid()) target = m_listSize;
    }
    target = target.expandedTo(m_window->minimumSize());
    if (const QRect area = placementArea(); area.isValid())
        target = target.boundedTo(area.size());

    // The panel comes out of the dock's top-left corner towards the bottom right,
    // which is where it was before folding; only when that does not fit (a dock
    // dragged against the right or bottom edge) does it open the other way.
    //
    // With size per page, first decide where the list would come out and place the
    // open page from there, as on a page switch (see m_pageHome).
    QRect from = QRect(anchoredTopLeft(dock, target), target);
    if (sizePerPage()) {
        QSize list = m_listSize.isValid() ? m_listSize : target;
        if (const QRect area = placementArea(); area.isValid()) list = list.boundedTo(area.size());
        m_pageHome = QRect(anchoredTopLeft(dock, list), list);
        from = m_pageHome;
    }
    const QPoint at = anchoredTopLeft(from, target);
    m_window->setGeometry(QRect(at, target));

    // Which point of the panel the dock came out of, to fold back through it. It
    // is what keeps a bottom-left dock bottom-left across open and close.
    m_dockOffset = QPoint(qBound(0, dock.x() - from.x(), qMax(0, from.width() - dock.width())),
                          qBound(0, dock.y() - from.y(), qMax(0, from.height() - dock.height())));

    keepOnScreen();
    // Folded with the planner open: collapse() gave back the narrow width before
    // saving it, and here it widens again.
    if (current() == m_widePage && !hadPageSize) enterWide();
    if (sizePerPage()) {
        m_grownFrom = m_grownTo = m_narrowGeom = m_wideGeom = QRect();
        if (current() == m_list) m_pageHome = m_window->geometry();
        m_pagePlaced = m_window->geometry();
    }
}

/// Where the window manager accepts the window: the available screen, grown by
/// the shadow margin (not visible frame, it may hang off the screen), and cut
/// to the WM's work area. That cut is what counts: a position outside it does
/// not fail, the WM corrects it and the window jumps.
QRect Navigator::placementArea(const QScreen *sc) const {
    if (!sc) sc = m_window->screen();
    if (!sc) return {};

    QRect area = sc->availableGeometry().adjusted(-shadowMargin(), -shadowMargin(),
                                                  shadowMargin(), shadowMargin());
    if (const QRect wm = wmWorkArea(); wm.isValid()) area &= wm;
    return area;
}

/// Where the window grows or shrinks from. Not "towards the screen centre":
/// that sent the dock to the far side of the monitor as soon as the panel
/// passed the middle. What decides is whether it fits: the top-left corner
/// stays, and the opposite edge is anchored only when it would leave the area.
QPoint Navigator::anchoredTopLeft(const QRect &before, const QSize &after) const {
    const QRect area = placementArea();
    if (!area.isValid()) return before.topLeft();

    // The shadow margin is the same on both sides, so aligning the window edges
    // aligns the visible frame too.
    return {anchorAxis(before.left(), before.width(), after.width(),
                       area.left(), area.right() + 1),
            anchorAxis(before.top(), before.height(), after.height(),
                       area.top(), area.bottom() + 1)};
}

/// Safety net: after folding or unfolding the window must not be left outside
/// the placement area. anchoredTopLeft() already accounts for it, but a
/// restored size or a screen change may leave it hanging out. On Wayland
/// move() may do nothing.
void Navigator::keepOnScreen() {
    const QRect area = placementArea();
    if (!area.isValid()) return;

    // qMin before qMax inside clampInto(): if the window does not fit it stays
    // anchored top-left instead of going off the other side.
    if (const QPoint p = clampInto(m_window->pos(), m_window->size(), area); p != m_window->pos())
        m_window->move(p);
}

/// Back to where the window was left last time. Without this every start
/// (and rebooting is a start) lets the window manager decide. On Wayland this
/// does nothing; on X11 it applies.
void Navigator::restoreWindowPos() {
    if (!m_store->prefs().hasWindowPos) return;
    const QPoint saved = m_store->prefs().windowPos;

    // The screen under the saved window, not the primary: with two monitors,
    // correcting against the primary would drag a window from the other one.
    // Looked up by the centre, since the corner's shadow margin may hang off
    // screen. If that screen is gone, placementArea() uses the current one and
    // the clamp brings the window back into view.
    const QScreen *sc = QGuiApplication::screenAt(QRect(saved, m_window->size()).center());
    const QRect area = placementArea(sc);
    m_window->move(area.isValid() ? clampInto(saved, m_window->size(), area) : saved);
}

void Navigator::mapped() {
    // On first map the window manager places the window wherever it likes,
    // overriding the requested position (KDE session restore, which runs on
    // reboot, does exactly that). It is asked again once, with a native window;
    // after that the window belongs to the user.
    if (m_posRestored) return;
    m_posRestored = true;
    restoreWindowPos();
}

void Navigator::syncPrefs() {
    // Maximised, what is kept is the size and place it will be restored to.
    if (m_window->isMaximized()) return;
    // Folded, the size that counts is the expanded one.
    const bool isFolded = folded();
    Store::Prefs &prefs = m_store->prefs();
    if (sizePerPage()) {
        // The window size saved is the list's, which opens at startup; the open
        // page's goes to its own slot.
        if (!isFolded && m_body) rememberPageSize(current());
        prefs.windowSize = m_listSize.isValid() ? m_listSize : m_window->size();
    } else {
        // Widened for the planner, what counts is the size before: the list opens
        // at startup, not the planner.
        const bool wide = m_narrowGeom.isValid() && m_window->geometry() == m_wideGeom;
        prefs.windowSize = isFolded ? m_expandedSize
                         : wide     ? m_narrowGeom.size()
                                    : m_window->size();
    }

    // The position is saved as it is, folded or not: at startup the panel opens
    // from that corner, which is exactly what unfolding the dock does.
    prefs.windowPos = m_window->pos();
    prefs.hasWindowPos = true;
}
