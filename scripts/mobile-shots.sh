#!/bin/sh
# Capturas de la interfaz móvil sin teléfono ni CI.
#
# Compila Tagoror de escritorio con el recorrido de capturas
# (-DTAGOROR_DEV_TOOLS=ON) y lo lanza sin pantalla (QT_QPA_PLATFORM=offscreen)
# en modo móvil forzado (TAGOROR_FORCE_MOBILE=1), una vez por tamaño de
# teléfono. Cada pasada guarda un PNG por pestaña y diálogo y un report.txt con
# lo que se sale por la derecha, si la barra de navegación está pegada abajo y
# qué botones miden menos de 44 px de alto.
#
# Uso:
#   scripts/mobile-shots.sh [carpeta-de-salida] [tamaño...]
#   scripts/mobile-shots.sh shots 360x640 412x915      (lo de por defecto)
#
# Necesita Qt 6 de escritorio con Multimedia: CMAKE_PREFIX_PATH apuntando a
# ella, como para cualquier compilación. TAGOROR_CMAKE_ARGS se pasa tal cual a
# cmake (p. ej. rutas de OpenGL en una máquina sin las cabeceras instaladas), y
# TAGOROR_SHOTS_BUILD cambia la carpeta de compilación (build-shots).
#
# Los datos son de usar y tirar: cada pasada corre con XDG_DATA_HOME,
# XDG_CONFIG_HOME y HOME en una carpeta temporal, nunca con tus notas.

set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-shots}"
[ $# -gt 0 ] && shift
sizes="${*:-360x640 412x915}"
build="${TAGOROR_SHOTS_BUILD:-$root/build-shots}"

# shellcheck disable=SC2086  # TAGOROR_CMAKE_ARGS son varios argumentos a propósito
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DTAGOROR_DEV_TOOLS=ON ${TAGOROR_CMAKE_ARGS:-} > /dev/null
cmake --build "$build" --parallel

mkdir -p "$out"
out="$(cd "$out" && pwd)"

for size in $sizes; do
    w="${size%x*}"
    h="${size#*x}"
    tmp="$(mktemp -d)"
    # La pantalla virtual mide lo que el teléfono, a 2x para que las capturas se
    # lean; sin marco de ventana, como en un móvil.
    cat > "$tmp/screen.json" <<EOF
{ "windowFrameMargins": false,
  "screens": [ { "name": "phone", "x": 0, "y": 0, "width": $w, "height": $h,
                 "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 2 } ] }
EOF
    rm -rf "${out:?}/$size"
    echo "== $size"
    HOME="$tmp" XDG_DATA_HOME="$tmp/data" XDG_CONFIG_HOME="$tmp/config" \
        XDG_RUNTIME_DIR="$tmp" LANG=es_ES.UTF-8 LANGUAGE=es \
        QT_QPA_PLATFORM="offscreen:configfile=$tmp/screen.json" \
        TAGOROR_FORCE_MOBILE=1 TAGOROR_MOBILE_SIZE="$size" TAGOROR_SHOTS_DIR="$out/$size" \
        "$build/tagoror" 2>&1 | grep -v "This plugin does not support" || true
    rm -rf "$tmp"
    echo "   $(ls "$out/$size"/*.png | wc -l) capturas en $out/$size"
done
