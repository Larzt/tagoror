#!/bin/sh
# Regenera los PNG por tamaño a partir de los SVG. Ejecutar cuando cambie
# alguno de los dos iconos; los cargadores usan el bitmap exacto si existe.
#
#   sh packaging/render-icons.sh
set -eu
cd "$(dirname "$0")/.."

for size in 16 22 24 32; do
    mkdir -p "packaging/icons/${size}x${size}"
    rsvg-convert -w "$size" -h "$size" codex-small.svg -o "packaging/icons/${size}x${size}/codex.png"
done
for size in 48 64 128 256; do
    mkdir -p "packaging/icons/${size}x${size}"
    rsvg-convert -w "$size" -h "$size" codex.svg -o "packaging/icons/${size}x${size}/codex.png"
done
# El icono de Windows lleva todos los tamaños dentro de un solo fichero, con
# el mismo reparto: el simplificado hasta 32 px y el detallado a partir de 48.
if command -v magick >/dev/null 2>&1; then
    magick packaging/icons/16x16/codex.png packaging/icons/24x24/codex.png \
           packaging/icons/32x32/codex.png packaging/icons/48x48/codex.png \
           packaging/icons/64x64/codex.png packaging/icons/128x128/codex.png \
           packaging/icons/256x256/codex.png packaging/windows/codex.ico
    echo "packaging/windows/codex.ico regenerado"
else
    echo "sin ImageMagick: packaging/windows/codex.ico se queda como estaba"
fi

echo "iconos regenerados en packaging/icons/"
