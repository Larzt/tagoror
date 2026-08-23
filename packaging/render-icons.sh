#!/bin/sh
# Regenera los PNG por tamaño a partir de los SVG. Ejecutar cuando cambie
# alguno de los dos iconos; los cargadores usan el bitmap exacto si existe.
#
#   sh packaging/render-icons.sh
set -eu
cd "$(dirname "$0")/.."

for size in 16 22 24 32; do
    mkdir -p "packaging/icons/${size}x${size}"
    rsvg-convert -w "$size" -h "$size" tagoror-small.svg -o "packaging/icons/${size}x${size}/tagoror.png"
done
for size in 48 64 128 256; do
    mkdir -p "packaging/icons/${size}x${size}"
    rsvg-convert -w "$size" -h "$size" tagoror.svg -o "packaging/icons/${size}x${size}/tagoror.png"
done
# El icono de Windows lleva todos los tamaños dentro de un solo fichero, con
# el mismo reparto: el simplificado hasta 32 px y el detallado a partir de 48.
if command -v magick >/dev/null 2>&1; then
    magick packaging/icons/16x16/tagoror.png packaging/icons/24x24/tagoror.png \
           packaging/icons/32x32/tagoror.png packaging/icons/48x48/tagoror.png \
           packaging/icons/64x64/tagoror.png packaging/icons/128x128/tagoror.png \
           packaging/icons/256x256/tagoror.png packaging/windows/tagoror.ico
    echo "packaging/windows/tagoror.ico regenerado"
else
    echo "sin ImageMagick: packaging/windows/tagoror.ico se queda como estaba"
fi

echo "iconos regenerados en packaging/icons/"
