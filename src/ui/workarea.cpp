#include "ui/workarea.hpp"

#include <QGuiApplication>
#include <QList>
#include <QScreen>

#ifdef CODEX_HAVE_XCB
#include <xcb/xcb.h>

#include <cstdlib>
#include <cstring>
#endif

namespace {

#ifdef CODEX_HAVE_XCB

// La conexión de la sesión, o nullptr si esto no es X11.
xcb_connection_t *x11Connection() {
    using namespace QNativeInterface;
    auto *x11 = qGuiApp ? qGuiApp->nativeInterface<QX11Application>() : nullptr;
    return x11 ? x11->connection() : nullptr;
}

// Solo átomos que ya existan (only_if_exists): si el gestor no publica la
// propiedad no hay nada que leer ni que pedirle.
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

// Un gestor EWMH (KWin entre ellos) no deja ninguna ventana fuera del área de
// trabajo: si se le pide una posición que se sale, la corrige él y la ventana
// aparece donde no se pidió. Y esa área no es la que Qt cuenta en
// QScreen::availableGeometry() -- medido en un escritorio de dos monitores:
// _NET_WORKAREA es un único rectángulo para todo el escritorio virtual, así que
// el panel de 32 px del monitor de 1080 recorta por abajo también al de 1440,
// que Qt sigue dando entero (2560x1440 disponibles frente a un área real de
// 4480x1048). Sin leer esto, plegar el panel en la mitad baja del monitor
// grande pedía una esquina que el gestor deshacía y la ventana daba un salto
// hacia arriba.
QRect wmWorkArea() {
#ifdef CODEX_HAVE_XCB
    xcb_connection_t *c = x11Connection();
    if (!c) return {};                         // no es X11: no hay propiedad que leer

    const auto atom = [c](const char *name) { return atomOf(c, name); };

    const xcb_atom_t workArea = atom("_NET_WORKAREA");
    if (workArea == XCB_ATOM_NONE) return {};

    const xcb_screen_t *root = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    if (!root) return {};

    // La propiedad lleva cuatro CARD32 por escritorio virtual; vale el actual.
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

    // La propiedad viene en píxeles físicos y Qt coloca en lógicos: con un
    // factor de escala distinto de 1 hay que convertirla.
    if (area.isValid())
        if (const QScreen *sc = QGuiApplication::primaryScreen(); sc && sc->devicePixelRatio() > 1.0)
            area = QRect(area.topLeft() / sc->devicePixelRatio(),
                         area.size() / sc->devicePixelRatio());
    return area;
#else
    return {};
#endif
}

// Qt no tiene forma de pedir esto: Qt::Tool marca la ventana como utilidad,
// que no basta -- el gestor de tareas la sigue listando. La propiedad se
// escribe *y* se manda como mensaje al root porque el reparto depende de si
// la ventana ya está mapeada: sin mapear manda la propiedad, mapeada solo
// vale el mensaje. Y la propiedad se lee antes para añadir a lo que haya:
// ahí es donde Qt guarda el _NET_WM_STATE_ABOVE/_BELOW de "siempre encima".
void wmSkipTaskbar(WId window) {
#ifdef CODEX_HAVE_XCB
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
        ev.data.data32[3] = 1;              // origen: la propia aplicación
        xcb_send_event(c, 0, root->root,
                       XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                       reinterpret_cast<const char *>(&ev));
    }
    xcb_flush(c);
#else
    Q_UNUSED(window);
#endif
}
