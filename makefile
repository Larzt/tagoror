BUILD   := build
RELEASE := build-release
BIN     := $(BUILD)/codex

# Por defecto se instala en el home: no hace falta root y el escritorio mira
# ahí igual que en /usr. Con PREFIX=/usr/local hace falta sudo.
PREFIX  ?= $(HOME)/.local
AUTOSTART := $(HOME)/.config/autostart/codex.desktop

.PHONY: all run clean configure install uninstall autostart autostart-off \
        appimage archpkg flatpak

all: configure
	cmake --build $(BUILD)

configure:
	@cmake -B $(BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Debug

run: all
	./$(BIN)

clean:
	rm -rf $(BUILD) $(RELEASE)

# Compilación optimizada aparte, para no reconfigurar la de desarrollo.
install:
	cmake -B $(RELEASE) -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(PREFIX)
	cmake --build $(RELEASE)
	cmake --install $(RELEASE)
	@update-desktop-database $(PREFIX)/share/applications 2>/dev/null || true
	@gtk-update-icon-cache -f -t $(PREFIX)/share/icons/hicolor 2>/dev/null || true
	@echo
	@echo "Instalado en $(PREFIX). Busca 'Códice' en el menú de aplicaciones."

uninstall:
	rm -f $(PREFIX)/bin/codex
	rm -f $(PREFIX)/share/applications/codex.desktop
	rm -f $(PREFIX)/share/icons/hicolor/scalable/apps/codex.svg
	rm -f $(PREFIX)/share/icons/hicolor/*/apps/codex.png
	rm -f $(AUTOSTART)
	@update-desktop-database $(PREFIX)/share/applications 2>/dev/null || true
	@echo "Desinstalado de $(PREFIX). Tus notas siguen en ~/.local/share/Stride/Codex."

# Arrancar con la sesión: copia la entrada instalada a ~/.config/autostart.
autostart:
	@install -Dm644 $(PREFIX)/share/applications/codex.desktop $(AUTOSTART)
	@echo "Se abrirá al iniciar sesión."

autostart-off:
	@rm -f $(AUTOSTART)
	@echo "Ya no se abrirá al iniciar sesión."

# --- paquetes para distribuir ------------------------------------------------
# Ver README ("Packaging it for others"): el AppImage se compila contra la glibc
# de esta máquina, y el paquete de Arch se baja el tarball de la etiqueta, que
# tiene que estar publicada.
appimage:
	sh packaging/appimage/build-appimage.sh

archpkg:
	cd packaging/arch && makepkg -f

flatpak:
	flatpak-builder --user --install --force-clean $(RELEASE)-flatpak \
	    packaging/flatpak/io.github.larzt.codex.yml
