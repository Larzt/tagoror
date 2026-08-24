#!/bin/sh
# Construye el instalador gráfico: un solo fichero que se abre con doble clic,
# pregunta cuatro cosas y deja Tagoror en el menú de aplicaciones.
#
#   sh packaging/installer/make-installer.sh [versión] [ruta al AppImage]
#
# Deja Tagoror-<versión>-x86_64-installer.run en la raíz del repositorio.
#
# Dentro va el AppImage entero, que lleva Qt consigo: por eso el instalador
# pesa lo que pesa (~95 MB) y por eso funciona en una Debian o una Fedora sin
# instalar nada más. Es la misma decisión que en Windows, donde windeployqt
# mete Qt al lado del .exe.
#
# OJO CON LA GLIBC: lo que se hereda del AppImage se hereda también aquí. Uno
# compilado en Arch no arranca en una Ubuntu LTS; para publicar, construir el
# AppImage dentro de una base vieja (ver packaging/appimage/build-appimage.sh)
# y pasárselo a este script como segundo argumento.
set -eu

cd "$(dirname "$0")/../.."

# La versión sale de CMakeLists.txt, que es donde vive de verdad.
VERSION="${1:-$(sed -n 's/^project(tagoror VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt)}"
[ -n "$VERSION" ] || { echo "no encuentro la versión en CMakeLists.txt" >&2; exit 1; }

APPIMAGE="${2:-}"
if [ -z "$APPIMAGE" ]; then
    APPIMAGE="Tagoror-$VERSION-x86_64.AppImage"
    if [ ! -f "$APPIMAGE" ]; then
        echo "· no hay $APPIMAGE todavía: lo construyo"
        sh packaging/appimage/build-appimage.sh "$VERSION"
    fi
fi
[ -f "$APPIMAGE" ] || { echo "no existe $APPIMAGE" >&2; exit 1; }

OUT="Tagoror-$VERSION-x86_64-installer.run"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT INT TERM

echo "· recogiendo la carga"
cp "$APPIMAGE" "$STAGE/tagoror.AppImage"
cp LICENSE "$STAGE/LICENSE"

# Los iconos ya rasterizados, con el reparto de variantes que hace
# render-icons.sh (la sencilla hasta 32 px, la detallada por encima).
mkdir -p "$STAGE/icons"
for dir in packaging/icons/*x*/; do
    size="$(basename "$dir")"
    mkdir -p "$STAGE/icons/$size"
    cp "$dir/tagoror.png" "$STAGE/icons/$size/tagoror.png"
done
cp tagoror.svg "$STAGE/icons/tagoror.svg"

# La entrada del menú sale de la misma plantilla que la instalación normal, así
# que el nombre, la descripción y las palabras clave no pueden desviarse. Solo
# cambian las dos líneas que aquí no pueden ser las de siempre: Exec apunta al
# AppImage (lo rellena el instalador, que es quien sabe el home) e Icon al
# nombre del tema, no al identificador de Flatpak.
sed -e 's|^Exec=.*|Exec=@APPIMAGE@|' \
    -e 's|^Icon=.*|Icon=tagoror|' \
    packaging/tagoror.desktop.in > "$STAGE/tagoror.desktop"

if command -v desktop-file-validate >/dev/null 2>&1; then
    # Se valida con una ruta de mentira: con el marcador dentro, Exec no es una
    # línea válida y el aviso sería solo ruido.
    sed 's|@APPIMAGE@|/usr/bin/tagoror|' "$STAGE/tagoror.desktop" > "$STAGE/.check.desktop"
    desktop-file-validate "$STAGE/.check.desktop" || {
        echo "el .desktop no valida" >&2; exit 1; }
    rm -f "$STAGE/.check.desktop"
fi

echo "· empaquetando $OUT"
tar czf "$STAGE/payload.tar.gz" -C "$STAGE" \
    tagoror.AppImage LICENSE icons tagoror.desktop

sed "s|@VERSION@|$VERSION|g" packaging/installer/installer.sh > "$OUT"
cat "$STAGE/payload.tar.gz" >> "$OUT"
chmod +x "$OUT"

echo
echo "Listo: $OUT ($(du -h "$OUT" | cut -f1))"
echo "Doble clic, o desde una terminal:  ./$OUT"
