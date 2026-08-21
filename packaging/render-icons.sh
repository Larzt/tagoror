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
echo "iconos regenerados en packaging/icons/"
