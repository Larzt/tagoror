cmake_minimum_required(VERSION 3.25)
project(codex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

# El código está por capas: core no sabe de interfaz, ui no sabe de disco y
# audio solo del micrófono y de los WAV.
add_executable(codex
    src/main.cpp

    src/core/store.cpp
    include/core/note.hpp
    include/core/paths.hpp
    include/core/store.hpp

    src/ui/panel.cpp
    src/ui/notecard.cpp
    src/ui/calendar.cpp
    src/ui/popup.cpp
    src/ui/waveform.cpp
    include/ui/panel.hpp
    include/ui/notecard.hpp
    include/ui/calendar.hpp
    include/ui/popup.hpp
    include/ui/theme.hpp
    include/ui/waveform.hpp
    include/ui/dragwidgets.hpp
    include/ui/elidedlabel.hpp

    src/audio/recorder.cpp
    src/audio/alarm.cpp
    include/audio/recorder.hpp
    include/audio/alarm.hpp
    include/audio/wave.hpp
)
# Iconos empotrados: la ventana los tiene aunque se ejecute sin instalar o el
# tema no los resuelva. Van los PNG y no los SVG porque QIcon solo elige bien
# entre variantes distintas cuando cada tamaño es un bitmap exacto.
qt_add_resources(codex "appicon"
    PREFIX "/icons"
    BASE "packaging/icons"
    FILES
        packaging/icons/16x16/codex.png
        packaging/icons/24x24/codex.png
        packaging/icons/32x32/codex.png
        packaging/icons/48x48/codex.png
        packaging/icons/64x64/codex.png
        packaging/icons/128x128/codex.png
        packaging/icons/256x256/codex.png)

target_include_directories(codex PRIVATE include)
target_link_libraries(codex PRIVATE Qt6::Widgets Qt6::Multimedia)

# --- instalación como aplicación de escritorio -------------------------------
include(GNUInstallDirs)

# El .desktop lleva la ruta absoluta del binario: así funciona aunque el
# prefijo elegido (p. ej. ~/.local) no esté en el PATH del entorno gráfico.
configure_file(packaging/codex.desktop.in
               ${CMAKE_CURRENT_BINARY_DIR}/codex.desktop @ONLY)

install(TARGETS codex RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/codex.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

# El SVG detallado como icono escalable, y PNG ya rasterizados por tamaño:
# los cargadores de iconos prefieren el bitmap exacto antes que el escalable
# (comprobado), que es justo lo que permite usar la variante simplificada por
# debajo de 32 px, donde los cinco anillos del detallado se emborronan.
# Los PNG se generan con packaging/render-icons.sh cuando cambien los SVG.
install(FILES codex.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
foreach(size 16x16 22x22 24x24 32x32 48x48 64x64 128x128 256x256)
    install(FILES packaging/icons/${size}/codex.png
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}/apps)
endforeach()
