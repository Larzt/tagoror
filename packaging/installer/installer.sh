#!/bin/sh
# Cabecera del instalador gráfico. No se usa suelta: make-installer.sh le pega
# detrás un tar.gz con el AppImage y los iconos, y lo que sale de ahí es el
# fichero de un solo doble clic.
#
# Instala en el home, sin root y sin pedir contraseña, igual que el instalador
# de Windows (PrivilegesRequired=lowest). Lo que deja:
#
#   ~/.local/opt/Tagoror/tagoror.AppImage   la aplicación, con Qt dentro
#   ~/.local/opt/Tagoror/uninstall.sh       para quitarla
#   ~/.local/bin/tagoror                    enlace, para lanzarla por su nombre
#   ~/.local/share/applications/…           la entrada del menú
#   ~/.local/share/icons/hicolor/…          los iconos, en todos sus tamaños
#
# Las notas no se tocan ni al instalar, ni al actualizar, ni al desinstalar.
set -eu

VERSION="@VERSION@"

PREFIX="$HOME/.local"
APPDIR="$PREFIX/opt/Tagoror"
APPIMAGE="$APPDIR/tagoror.AppImage"
DESKTOP="$PREFIX/share/applications/tagoror.desktop"
AUTOSTART="$HOME/.config/autostart/tagoror.desktop"
ICONS="$PREFIX/share/icons/hicolor"

# --- idioma ----------------------------------------------------------------
# El mismo par que habla la aplicación. Aquí no hay tabla que valga la pena:
# son diez frases.
case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in
    es*) LANG_ES=1 ;;
    *)   LANG_ES=0 ;;
esac

say() {
    if [ "$LANG_ES" = 1 ]; then
        case "$1" in
        title)    printf 'Instalar Tagoror %s' "$VERSION" ;;
        titleup)  printf 'Actualizar Tagoror a %s' "$VERSION" ;;
        intro)    printf 'Se instalará en %s\n\nNo hace falta contraseña: todo va a tu carpeta personal, y tus notas no se tocan.' "$APPDIR" ;;
        introup)  printf 'Ya hay una copia instalada en %s.\n\nSe reemplazará por la %s. Tus notas se quedan como están.' "$APPDIR" "$VERSION" ;;
        opts)     printf 'Opciones' ;;
        optmenu)  printf 'Añadir al menú de aplicaciones' ;;
        optauto)  printf 'Abrir al iniciar sesión' ;;
        working)  printf 'Instalando…' ;;
        done)     printf 'Tagoror %s instalado.\n\nLo tienes en el menú de aplicaciones, o escribiendo «tagoror» en una terminal.\n\nPara quitarlo: %s/uninstall.sh\n\n¿Lo abro ahora?' "$VERSION" "$APPDIR" ;;
        nofuse)   printf '\n\nAviso: no encuentro FUSE en este sistema y un AppImage lo necesita. Si no arranca, instala «fuse2» (en Debian o Ubuntu, «libfuse2»).' ;;
        cancel)   printf 'Instalación cancelada.' ;;
        askrun)   printf '¿Abrir Tagoror ahora? [S/n] ' ;;
        yes)      printf 'S' ;;
        esac
    else
        case "$1" in
        title)    printf 'Install Tagoror %s' "$VERSION" ;;
        titleup)  printf 'Update Tagoror to %s' "$VERSION" ;;
        intro)    printf 'It will be installed in %s\n\nNo password needed: everything goes in your home folder, and your notes are left alone.' "$APPDIR" ;;
        introup)  printf 'There is already a copy in %s.\n\nIt will be replaced with %s. Your notes stay as they are.' "$APPDIR" "$VERSION" ;;
        opts)     printf 'Options' ;;
        optmenu)  printf 'Add to the application menu' ;;
        optauto)  printf 'Open when I sign in' ;;
        working)  printf 'Installing…' ;;
        done)     printf 'Tagoror %s installed.\n\nIt is in your application menu, or type «tagoror» in a terminal.\n\nTo remove it: %s/uninstall.sh\n\nOpen it now?' "$VERSION" "$APPDIR" ;;
        nofuse)   printf '\n\nHeads-up: FUSE does not seem to be available, and an AppImage needs it. If it will not start, install «fuse2» («libfuse2» on Debian or Ubuntu).' ;;
        cancel)   printf 'Installation cancelled.' ;;
        askrun)   printf 'Open Tagoror now? [Y/n] ' ;;
        yes)      printf 'Y' ;;
        esac
    fi
}

# --- con qué se pregunta ---------------------------------------------------
# kdialog en Plasma y zenity en el resto; sin ninguno de los dos (o sin sesión
# gráfica) se pregunta por la terminal, que es donde estará mirando quien lo
# haya lanzado así.
UI=text
if [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    if command -v kdialog >/dev/null 2>&1; then UI=kdialog
    elif command -v zenity >/dev/null 2>&1; then UI=zenity
    fi
fi

WANT_MENU=1
WANT_AUTOSTART=0
[ -f "$APPIMAGE" ] && UPDATE=1 || UPDATE=0
[ "$UPDATE" = 1 ] && TITLE="$(say titleup)" || TITLE="$(say title)"
[ "$UPDATE" = 1 ] && INTRO="$(say introup)" || INTRO="$(say intro)"

case "$UI" in
kdialog)
    # --separate-output: una etiqueta por línea, sin comillas que desenredar.
    kdialog --title "$TITLE" --yesno "$INTRO" || { kdialog --title "$TITLE" --msgbox "$(say cancel)"; exit 1; }
    CHOICE="$(kdialog --title "$TITLE" --separate-output --checklist "$(say opts)" \
        menu "$(say optmenu)" on \
        auto "$(say optauto)" off || true)"
    ;;
zenity)
    zenity --question --title="$TITLE" --text="$INTRO" --no-markup || { zenity --info --text="$(say cancel)" --no-markup; exit 1; }
    CHOICE="$(zenity --list --checklist --title="$TITLE" --text="$(say opts)" \
        --column='' --column='id' --column='' --hide-column=2 --print-column=2 \
        --separator='
' TRUE menu "$(say optmenu)" FALSE auto "$(say optauto)" || true)"
    ;;
*)
    # Sin diálogos no se pregunta nada: entrada en el menú sí, arranque
    # automático no, que es lo que ya traen los valores de arriba.
    printf '%s\n\n%s\n\n' "$TITLE" "$INTRO"
    ;;
esac

case "$UI" in
kdialog|zenity)
    case "$CHOICE" in *menu*) WANT_MENU=1 ;; *) WANT_MENU=0 ;; esac
    case "$CHOICE" in *auto*) WANT_AUTOSTART=1 ;; *) WANT_AUTOSTART=0 ;; esac
    ;;
esac

# --- desempaquetar ---------------------------------------------------------
# La carga va detrás de la marca; se cuenta la línea en la que empieza y de ahí
# al tar. awk se para en la marca, así que no llega a leer el binario.
PAYLOAD_LINE="$(awk '/^__PAYLOAD_BELOW__$/ { print NR + 1; exit 0 }' "$0")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM
tail -n +"$PAYLOAD_LINE" "$0" | tar xz -C "$TMP"

mkdir -p "$APPDIR" "$PREFIX/bin" "$PREFIX/share/applications"
install -m 755 "$TMP/tagoror.AppImage" "$APPIMAGE"
install -m 644 "$TMP/LICENSE" "$APPDIR/LICENSE"
ln -sf "$APPIMAGE" "$PREFIX/bin/tagoror"

# Los iconos van igual instale o no la entrada del menú: la ventana los pide
# por nombre al tema de iconos, no al .desktop.
for dir in "$TMP"/icons/*x*/; do
    size="$(basename "$dir")"
    install -Dm644 "$dir/tagoror.png" "$ICONS/$size/apps/tagoror.png"
done
install -Dm644 "$TMP/icons/tagoror.svg" "$ICONS/scalable/apps/tagoror.svg"

if [ "$WANT_MENU" = 1 ]; then
    # La plantilla sale del .desktop del proyecto, con Exec apuntando al
    # AppImage en vez de a un /usr/bin que aquí no existe.
    sed "s|@APPIMAGE@|$APPIMAGE|" "$TMP/tagoror.desktop" > "$DESKTOP"
    chmod 644 "$DESKTOP"
else
    rm -f "$DESKTOP"
fi

if [ "$WANT_AUTOSTART" = 1 ]; then
    mkdir -p "$(dirname "$AUTOSTART")"
    sed "s|@APPIMAGE@|$APPIMAGE|" "$TMP/tagoror.desktop" > "$AUTOSTART"
    chmod 644 "$AUTOSTART"
else
    rm -f "$AUTOSTART"
fi

# --- desinstalador ---------------------------------------------------------
# Se escribe con las rutas ya resueltas: quien lo ejecute no tiene que saber
# nada, y no borra nada que no haya puesto el instalador.
cat > "$APPDIR/uninstall.sh" <<EOF
#!/bin/sh
# Quita Tagoror de este usuario. Tus notas NO se borran: siguen donde estén,
# por defecto en ~/.local/share/Stride/Tagoror (o en la carpeta que hayas
# elegido en los ajustes).
set -eu
rm -f "$PREFIX/bin/tagoror" "$DESKTOP" "$AUTOSTART"
rm -f "$ICONS"/*/apps/tagoror.png "$ICONS/scalable/apps/tagoror.svg"
rm -rf "$APPDIR"
update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
gtk-update-icon-cache -f -t "$ICONS" 2>/dev/null || true
echo "Tagoror desinstalado. Tus notas siguen donde estaban."
EOF
chmod 755 "$APPDIR/uninstall.sh"

update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
gtk-update-icon-cache -f -t "$ICONS" 2>/dev/null || true

# --- final -----------------------------------------------------------------
MSG="$(say done)"
# Un AppImage se monta con FUSE; sin él no arranca y el fallo no dice por qué.
if ! command -v fusermount >/dev/null 2>&1 &&
   ! command -v fusermount3 >/dev/null 2>&1 &&
   [ ! -e /dev/fuse ]; then
    MSG="$MSG$(say nofuse)"
fi

launch() { setsid "$APPIMAGE" >/dev/null 2>&1 & }

case "$UI" in
kdialog) kdialog --title "$TITLE" --yesno "$MSG" && launch || true ;;
zenity)  zenity --question --title="$TITLE" --text="$MSG" --no-markup && launch || true ;;
*)
    printf '%s\n' "$MSG"
    printf '%s' "$(say askrun)"
    read -r answer || answer=""
    case "$answer" in ""|s|S|y|Y) launch ;; esac
    ;;
esac

exit 0
__PAYLOAD_BELOW__
