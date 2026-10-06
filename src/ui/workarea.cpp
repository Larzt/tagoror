#include "ui/workarea.hpp"

#include <QGuiApplication>
#include <QList>
#include <QScreen>

#ifdef TAGOROR_HAVE_XCB
#include <xcb/xcb.h>

#include <cstdlib>
#include <cstring>
#endif

namespace {

#ifdef TAGOROR_HAVE_XCB

/// The session's X11 connection, or nullptr off X11.
xcb_connection_t *x11Connection() {
    using namespace QNativeInterface;
    auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QX11Application>() : nullptr;
    return x11 ? x11->connection() : nullptr;
}

/// Existing atoms only (only_if_exists): if the WM does not publish the
/// property there is nothing to read or ask for.
xcb_atom_t atomOf(xcb_connection_t *c, const char *name) {
    const xcb_intern_atom_cookie_t ck = xcb_intern_atom(c, 1, uint16_t(std::strlen(name)), name);
    xcb_atom_t a = XCB_ATOM_NONE;
    if (xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, ck, nullptr)) {
        a = r->atom;
        std::free(r);
    }
    return a;
}

#endif

}  // namespace

/// An EWMH window manager (KWin among them) never lets a window outside the
/// work area: it silently corrects the request and the window jumps. That
/// area is not what QScreen::availableGeometry() reports: measured on two
/// monitors, _NET_WORKAREA is one rectangle for the whole virtual desktop, so
/// the 32px panel of the 1080 screen also cuts the 1440 one, which Qt still
/// reports whole (2560x1440 available against a real 4480x1048).
QRect wmWorkArea() {
#ifdef TAGOROR_HAVE_XCB
    xcb_connection_t *c = x11Connection();
    if (!c) return {};                         // not X11: no property to read

    const auto atom = [c](const char *name) { return atomOf(c, name); };

    const xcb_atom_t workArea = atom("_NET_WORKAREA");
    if (workArea == XCB_ATOM_NONE) return {};

    const xcb_screen_t *root = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    if (!root) return {};

    // Four CARD32 per virtual desktop; the current one applies.
    uint32_t desktop = 0;
    if (const xcb_atom_t current = atom("_NET_CURRENT_DESKTOP"); current != XCB_ATOM_NONE) {
        const xcb_get_property_cookie_t ck =
            xcb_get_property(c, 0, root->root, current, XCB_ATOM_CARDINAL, 0, 1);
        if (xcb_get_property_reply_t *r = xcb_get_property_reply(c, ck, nullptr)) {
            if (xcb_get_property_value_length(r) >= 4)
                desktop = *static_cast<uint32_t *>(xcb_get_property_value(r));
            std::free(r);
        }
    }

    const uint32_t words = (desktop + 1) * 4;
    const xcb_get_property_cookie_t ck =
        xcb_get_property(c, 0, root->root, workArea, XCB_ATOM_CARDINAL, 0, words);
    xcb_get_property_reply_t *r = xcb_get_property_reply(c, ck, nullptr);
    if (!r) return {};

    QRect area;
    if (uint32_t(xcb_get_property_value_length(r)) >= words * 4) {
        const auto *v = static_cast<const uint32_t *>(xcb_get_property_value(r)) + desktop * 4;
        area = QRect(int(v[0]), int(v[1]), int(v[2]), int(v[3]));
    }
    std::free(r);

    // The property is in physical pixels and Qt places in logical ones.
    if (area.isValid())
        if (const QScreen *sc = QGuiApplication::primaryScreen(); sc && sc->devicePixelRatio() > 1.0)
            area = QRect(area.topLeft() / sc->devicePixelRatio(),
                         area.size() / sc->devicePixelRatio());
    return area;
#else
    return {};
#endif
}

/// Qt cannot ask for this: Qt::Tool only marks the window as a utility, and
/// the task manager still lists it. The property is written *and* sent as a
/// client message to the root: before mapping the property counts, once
/// mapped only the message does. The property is read first and appended to,
/// since Qt keeps "always on top" (_NET_WM_STATE_ABOVE/_BELOW) there.
void wmSkipTaskbar(WId window) {
#ifdef TAGOROR_HAVE_XCB
    xcb_connection_t *c = x11Connection();
    if (!c || !window) return;

    const xcb_atom_t state = atomOf(c, "_NET_WM_STATE");
    const xcb_atom_t skipTaskbar = atomOf(c, "_NET_WM_STATE_SKIP_TASKBAR");
    const xcb_atom_t skipPager = atomOf(c, "_NET_WM_STATE_SKIP_PAGER");
    if (state == XCB_ATOM_NONE || skipTaskbar == XCB_ATOM_NONE) return;

    QList<xcb_atom_t> atoms;
    const xcb_get_property_cookie_t ck =
        xcb_get_property(c, 0, xcb_window_t(window), state, XCB_ATOM_ATOM, 0, 64);
    if (xcb_get_property_reply_t *r = xcb_get_property_reply(c, ck, nullptr)) {
        const auto *v = static_cast<const xcb_atom_t *>(xcb_get_property_value(r));
        const int n = xcb_get_property_value_length(r) / int(sizeof(xcb_atom_t));
        for (int i = 0; i < n; ++i) atoms.append(v[i]);
        std::free(r);
    }
    for (xcb_atom_t a : {skipTaskbar, skipPager})
        if (a != XCB_ATOM_NONE && !atoms.contains(a)) atoms.append(a);

    xcb_change_property(c, XCB_PROP_MODE_REPLACE, xcb_window_t(window), state, XCB_ATOM_ATOM, 32,
                        uint32_t(atoms.size()), atoms.constData());

    const xcb_screen_t *root = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    if (root) {
        xcb_client_message_event_t ev = {};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.format = 32;
        ev.window = xcb_window_t(window);
        ev.type = state;
        ev.data.data32[0] = 1;              // _NET_WM_STATE_ADD
        ev.data.data32[1] = skipTaskbar;
        ev.data.data32[2] = skipPager;
        ev.data.data32[3] = 1;              // source: the application itself
        xcb_send_event(c, 0, root->root,
                       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                       reinterpret_cast<const char *>(&ev));
    }
    xcb_flush(c);
#else
    Q_UNUSED(window);
#endif
}
