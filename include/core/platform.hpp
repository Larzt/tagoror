#pragma once

#include <QtGlobal>

/// What the platform has, in one place, so the rest of the code asks about
/// capabilities instead of repeating lists of Q_OS_* macros.
///
/// Careful with Q_OS_LINUX: Qt defines it on Android too. Whatever is about the
/// Linux desktop (X11, Wayland, .desktop files) must ask for TAGOROR_LINUX_DESKTOP.

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
/// Phone or tablet: no tray, no window manager, one instance guaranteed by the
/// system, the app installed and updated from outside.
#define TAGOROR_MOBILE 1
#endif

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
#define TAGOROR_LINUX_DESKTOP 1
#endif

/// Whether the interface behaves as on a phone: the window is the screen, no
/// widget chrome, Ajustes as a tab, the back button. Always on Android and iOS.
///
/// On the desktop, TAGOROR_FORCE_MOBILE=1 turns the same paths on to look at
/// the phone layout without a phone (see scripts/mobile-shots.sh). Only what
/// is about the interface asks this; what the platform simply lacks (tray,
/// microphone permission) stays behind TAGOROR_MOBILE.
#ifdef TAGOROR_MOBILE
inline bool mobileUi() { return true; }
#else
inline bool mobileUi() {
    static const bool forced = qEnvironmentVariableIntValue("TAGOROR_FORCE_MOBILE") == 1;
    return forced;
}
#endif
