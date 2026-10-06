#pragma once

#include <QRect>
#include <qwindowdefs.h>   // WId

/// The rectangle the window manager allows windows in (_NET_WORKAREA).
/// @return An invalid rect when unknown: off X11, or no WM publishing it.
QRect wmWorkArea();

/// Keeps the window out of the taskbar and pager (_NET_WM_STATE_SKIP_TASKBAR /
/// _SKIP_PAGER). A no-op off X11; on Wayland the tray icon is the answer.
void wmSkipTaskbar(WId window);
