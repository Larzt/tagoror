cmake_minimum_required(VERSION 3.25)
project(tagoror VERSION 1.2.0 LANGUAGES CXX)

# La versión que enseñan el .exe de Windows y su instalador. La CI la pisa
# con la de la etiqueta que se está publicando.
set(TAGOROR_VERSION "${PROJECT_VERSION}" CACHE STRING "Versión que se muestra en los paquetes")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

# El nombre del .desktop, el de los iconos instalados, el ID de AppStream y el
# que la ventana declara con setDesktopFileName() tienen que ser el mismo: de
# ahí saca el escritorio el nombre y el icono de la ventana, y en Wayland es lo
# único que los relaciona. Flatpak además exige que ese nombre sea el ID de la
# aplicación (io.github.larzt.tagoror), así que sale de una variable en vez de
# estar escrito en cuatro sitios. El binario se llama 'tagoror' pase lo que pase.
set(TAGOROR_APP_ID "tagoror" CACHE STRING "Nombre del .desktop, de los iconos y del ID de AppStream")

# El código está por capas: core no sabe de interfaz, ui no sabe de disco y
# audio solo del micrófono y de los WAV.
add_executable(tagoror
    src/main.cpp

    src/core/store.cpp
    src/core/lang.cpp
    include/core/lang.hpp
    include/core/note.hpp
    include/core/paths.hpp
    include/core/store.hpp

    src/ui/panel.cpp
    src/ui/notecard.cpp
    src/ui/calendar.cpp
    src/ui/popup.cpp
    src/ui/waveform.cpp
    src/ui/workarea.cpp
    include/ui/panel.hpp
    include/ui/notecard.hpp
    include/ui/calendar.hpp
    include/ui/popup.hpp
    include/ui/theme.hpp
    include/ui/waveform.hpp
    include/ui/workarea.hpp
    include/ui/dragwidgets.hpp
    include/ui/elidedlabel.hpp
    include/ui/imagethumb.hpp

    src/audio/recorder.cpp
    src/audio/alarm.cpp
    include/audio/recorder.hpp
    include/audio/alarm.hpp
    include/audio/wave.hpp
)

if (MSVC)
    # El código está en UTF-8 y va lleno de acentos: sin esto MSVC lo lee con
    # la página de códigos del sistema y "Tagoror" llega roto a la pantalla.
    target_compile_options(tagoror PRIVATE /utf-8)
endif()

if (WIN32)
    # Sin esto Windows abre una consola detrás de la ventana.
    set_target_properties(tagoror PROPERTIES WIN32_EXECUTABLE TRUE)

    # El .rc lleva el icono y la versión; se genera para no repetir la versión
    # en un segundo sitio. Si TAGOROR_VERSION no viene como x.y.z (una etiqueta
    # rara, una compilación a mano) se usa la del proyecto, porque VERSIONINFO
    # solo admite números.
    if (TAGOROR_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        set(TAGOROR_VERSION_MAJOR ${CMAKE_MATCH_1})
        set(TAGOROR_VERSION_MINOR ${CMAKE_MATCH_2})
        set(TAGOROR_VERSION_PATCH ${CMAKE_MATCH_3})
    else()
        set(TAGOROR_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
        set(TAGOROR_VERSION_MINOR ${PROJECT_VERSION_MINOR})
        set(TAGOROR_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    endif()

    # rc.exe entiende las barras normales; las de Windows habría que escaparlas.
    set(TAGOROR_WIN_ICON "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/tagoror.ico")
    configure_file(packaging/windows/tagoror.rc.in
                   ${CMAKE_CURRENT_BINARY_DIR}/tagoror.rc @ONLY)
    target_sources(tagoror PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/tagoror.rc)
endif()
# Iconos empotrados: la ventana los tiene aunque se ejecute sin instalar o el
# tema no los resuelva. Van los PNG y no los SVG porque QIcon solo elige bien
# entre variantes distintas cuando cada tamaño es un bitmap exacto.
qt_add_resources(tagoror "appicon"
    PREFIX "/icons"
    BASE "packaging/icons"
    FILES
        packaging/icons/16x16/tagoror.png
        packaging/icons/24x24/tagoror.png
        packaging/icons/32x32/tagoror.png
        packaging/icons/48x48/tagoror.png
        packaging/icons/64x64/tagoror.png
        packaging/icons/128x128/tagoror.png
        packaging/icons/256x256/tagoror.png)

target_include_directories(tagoror PRIVATE include)
target_compile_definitions(tagoror PRIVATE TAGOROR_APP_ID="${TAGOROR_APP_ID}")
target_link_libraries(tagoror PRIVATE Qt6::Widgets Qt6::Multimedia)

# El gestor de ventanas no admite colocar una ventana fuera de _NET_WORKAREA, y
# esa área no coincide con la que informa Qt (ver src/ui/workarea.cpp). Leer la
# propiedad pide libxcb, que ya está donde haya sesión gráfica X11; sin ella el
# widget funciona igual, solo que se fía de QScreen::availableGeometry().
# En Windows no hay nada que buscar: allí Qt::Tool ya deja la ventana fuera de
# la barra de tareas (WS_EX_TOOLWINDOW) y availableGeometry() sí es el área de
# trabajo de verdad, así que workarea.cpp se compila entero como nop.
if (UNIX AND NOT APPLE)
    find_library(TAGOROR_XCB_LIBRARY xcb)
    if (TAGOROR_XCB_LIBRARY)
        target_link_libraries(tagoror PRIVATE ${TAGOROR_XCB_LIBRARY})
        target_compile_definitions(tagoror PRIVATE TAGOROR_HAVE_XCB)
    else()
        message(STATUS "libxcb no encontrada: el panel se colocará solo con QScreen")
    endif()
endif()

# --- instalación --------------------------------------------------------------
include(GNUInstallDirs)

# En Windows no hay jerarquía que respetar: se instala en un directorio plano
# que luego rellena windeployqt y empaqueta Inno Setup (packaging/windows/).
if (WIN32)
    install(TARGETS tagoror RUNTIME DESTINATION .)
    # La licencia se instala al lado del .exe: es lo que la recoge windeployqt
    # en la carpeta que luego empaquetan el ZIP portable y Inno Setup, así que
    # las dos formas de repartirlo llevan el texto dentro.
    install(FILES LICENSE DESTINATION .)
    return()
endif()

# El .desktop lleva la ruta absoluta del binario: así funciona aunque el
# prefijo elegido (p. ej. ~/.local) no esté en el PATH del entorno gráfico.
configure_file(packaging/tagoror.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/${TAGOROR_APP_ID}.desktop @ONLY)

# Ficha para las tiendas de aplicaciones (Discover, GNOME Software) y
# obligatoria en Flathub.
configure_file(packaging/tagoror.metainfo.xml.in
               ${CMAKE_CURRENT_BINARY_DIR}/${TAGOROR_APP_ID}.metainfo.xml @ONLY)

install(TARGETS tagoror RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${TAGOROR_APP_ID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${TAGOROR_APP_ID}.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)

# El texto de la licencia. Arch lo exige para MIT: su texto lleva el nombre del
# autor, así que no es una de las licencias 'common' que ya están en el sistema
# y cada paquete tiene que traer la suya en /usr/share/licenses/<paquete>/.
# La carpeta va con el nombre del paquete (tagoror), no con TAGOROR_APP_ID: el
# ID solo manda en el .desktop, los iconos y la ficha de AppStream.
install(FILES LICENSE
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/tagoror)

# El SVG detallado como icono escalable, y PNG ya rasterizados por tamaño:
# los cargadores de iconos prefieren el bitmap exacto antes que el escalable
# (comprobado), que es justo lo que permite usar la variante simplificada por
# debajo de 32 px, donde los cinco anillos del detallado se emborronan.
# Los PNG se generan con packaging/render-icons.sh cuando cambien los SVG.
install(FILES tagoror.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
        RENAME ${TAGOROR_APP_ID}.svg)
foreach(size 16x16 22x22 24x24 32x32 48x48 64x64 128x128 256x256)
    install(FILES packaging/icons/${size}/tagoror.png
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}/apps
            RENAME ${TAGOROR_APP_ID}.png)
endforeach()
