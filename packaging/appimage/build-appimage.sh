#!/bin/sh
# Construye un AppImage: un solo fichero ejecutable que lleva Qt dentro, para
# quien no quiere (o no puede) instalar Qt 6 en su distribución.
#
#   sh packaging/appimage/build-appimage.sh [version]
#
# Deja Tagoror-<version>-x86_64.AppImage en la raíz del repositorio. Necesita red
# la primera vez: se baja linuxdeploy y su complemento de Qt a
# build-appimage/tools/ y ahí se quedan.
#
# OJO CON LA GLIBC: un AppImage no la lleva dentro, así que solo corre en
# sistemas con una glibc igual o más nueva que la de la máquina donde se
# compiló. Compilándolo en Arch (glibc muy reciente) el resultado no arranca en
# una Ubuntu LTS. Para publicar, compilarlo dentro de una base vieja:
#
#   docker run --rm -v "$PWD:/src" -w /src ubuntu:22.04 \
#       sh -c 'apt update && apt install -y qt6-base-dev qt6-multimedia-dev \
#              cmake ninja-build g++ libxcb1-dev file wget && \
#              sh packaging/appimage/build-appimage.sh'
set -eu

cd "$(dirname "$0")/../.."
VERSION="${1:-1.2}"

WORK="build-appimage"
TOOLS="$WORK/tools"
APPDIR="$WORK/AppDir"
BASE="https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous"
QTBASE="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous"

mkdir -p "$TOOLS"
fetch() {
    [ -f "$TOOLS/$1" ] && return 0
    echo "· bajando $1"
    wget -q --show-progress -O "$TOOLS/$1" "$2/$1"
    chmod +x "$TOOLS/$1"
}
fetch linuxdeploy-x86_64.AppImage "$BASE"
fetch linuxdeploy-plugin-qt-x86_64.AppImage "$QTBASE"

echo "· compilando (Release)"
cmake -S . -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr >/dev/null
cmake --build "$WORK/build"

rm -rf "$APPDIR"
DESTDIR="$(pwd)/$APPDIR" cmake --install "$WORK/build" >/dev/null

# El .desktop instalado apunta al binario por su ruta absoluta (/usr/bin/tagoror),
# que es justo lo que hace falta en una instalación normal y lo que no sirve
# aquí: dentro del AppImage esa ruta no existe hasta que se monta. linuxdeploy
# saca de Exec el nombre del ejecutable al que apuntará AppRun, así que en esta
# copia -- solo en esta -- se deja el nombre pelado.
sed -i 's|^Exec=.*|Exec=tagoror|' "$APPDIR/usr/share/applications/tagoror.desktop"

# Sin FUSE (contenedores, algunas distros) las herramientas no se pueden
# montar: que se descompriman ellas solas.
export APPIMAGE_EXTRACT_AND_RUN=1
# El complemento de Qt localiza la instalación preguntándole a qmake.
QMAKE="$(command -v qmake6 || command -v qmake-qt6 || command -v qmake)"
export QMAKE
# Los que no salen de las dependencias del binario: el reproductor carga su
# backend en tiempo de ejecución, y sin el plugin de plataforma no hay ventana.
export EXTRA_QT_PLUGINS="multimedia;tls"
# El strip que lleva linuxdeploy es más viejo que el enlazador de cualquier
# distribución al día y se atraganta con las secciones .relr.dyn ("unknown type
# [0x13]"), lo que aborta el empaquetado entero. No merece la pena por unos MB.
export NO_STRIP=1
export OUTPUT="Tagoror-$VERSION-x86_64.AppImage"

# En tres pasos y no en uno (`--plugin qt`) porque al complemento de Qt hay que
# pasarle --exclude-library, y a través de linuxdeploy no hay manera.
echo "· recogiendo el binario y sus bibliotecas"
"$TOOLS/linuxdeploy-x86_64.AppImage" \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/tagoror.desktop" \
    --icon-file "packaging/icons/256x256/tagoror.png"

# kimg_*: los formatos de imagen de KDE (JPEG XL, OpenEXR, JPEG XR...). No hace
# falta ninguno -- los iconos se dibujan en código y los del recurso son PNG,
# que Qt lee sin plugin -- y arrastran media distribución; encima kimg_jxr pide
# una libjxrglue que ni siquiera está instalada, y eso tumba el empaquetado.
echo "· metiendo Qt dentro"
"$TOOLS/linuxdeploy-plugin-qt-x86_64.AppImage" \
    --appdir "$APPDIR" \
    --exclude-library='kimg_*'

echo "· empaquetando $OUTPUT"
"$TOOLS/linuxdeploy-x86_64.AppImage" --appdir "$APPDIR" --output appimage

echo
echo "Listo: $OUTPUT"
echo "Pruébalo con  ./$OUTPUT"
