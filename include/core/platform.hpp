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

/// The same as a constant, for plain `if (kMobile)`: the desktop path stays
/// compiled (and checked) everywhere, and the compiler drops the dead branch.
#ifdef TAGOROR_MOBILE
inline constexpr bool kMobile = true;
#else
inline constexpr bool kMobile = false;
#endif
