#pragma once

#include <QRect>
#include <qwindowdefs.h>   // WId

// El rectángulo en el que el gestor de ventanas admite colocar una ventana
// (_NET_WORKAREA). Rect inválido cuando no se puede averiguar: fuera de X11, o
// sin un gestor que publique la propiedad.
QRect wmWorkArea();

// Marca la ventana para que ni la barra de tareas ni el paginador la listen
// (_NET_WM_STATE_SKIP_TASKBAR / _SKIP_PAGER). No hace nada fuera de X11: en
// Wayland no existe la propiedad y el sitio del icono lo ocupa la bandeja.
void wmSkipTaskbar(WId window);
