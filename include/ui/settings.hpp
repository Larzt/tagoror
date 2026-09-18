#pragma once

#include <QByteArray>
#include <QColor>
#include <QWidget>

#include "core/lang.hpp"
#include "core/store.hpp"
#include "ui/theme.hpp"

class QVBoxLayout;

// Página de ajustes: la cuarta de m_body, hermana del calendario y de los
// cumpleaños. Antes era un Popup colgando del botón del engranaje, y se le
// quedó pequeño: una lista de menú no tiene sitio para un interruptor, un
// deslizador y la ruta de una carpeta sin convertirse en una columna larguísima
// que además tapaba el panel entero.
//
// Como las otras páginas no toca disco ni guarda nada: lee del Store —que es de
// quien son las preferencias— y avisa hacia arriba de cada cambio para que el
// Panel lo aplique y lo guarde.
class SettingsView : public QWidget {
    Q_OBJECT

public:
    explicit SettingsView(const Theme &theme, QWidget *parent = nullptr);

    // El Store vive más que esta vista; se guarda el puntero para leer las
    // preferencias en cada repintado en vez de copiarlas.
    void setSource(const Store *store);
    // No rehace la página, a diferencia de las otras vistas: el deslizador de
    // la opacidad aplica en vivo, y cada movimiento acaba aquí — reconstruir
    // destruiría el propio deslizador que se está arrastrando. Lo que depende
    // del acento se repinta, que es todo lo que hace falta.
    void setTheme(const Theme &theme);

    void refresh();
    void retranslate() { refresh(); }

signals:
    void accentPicked(const QColor &c);
    void accentEditorRequested(QWidget *anchor);
    void opacityChanged(int value);          // en vivo, mientras se arrastra
    void languagePicked(Lang::Code code);
    void onTopToggled(bool on);
    void x11Toggled(bool on);
    void dataFolderRequested();
    void backupsRequested(QWidget *anchor);
    void inputPicked(const QByteArray &id);
    void quitRequested();

private:
    void addSection(const QString &title);
    void addAccent();
    void addOpacity();
    void addLanguage();
    void addWindow();
    void addData();
    void addInputs();
    void addQuit();
    // Fila con interruptor: rótulo a la izquierda, el mando a la derecha.
    void addToggle(const QString &label, const QString &hint, bool on,
                   std::function<void(bool)> changed);
    // Grupo de botones de los que solo uno está elegido.
    void addSegments(const QStringList &labels, int chosen, std::function<void(int)> picked);
    // Tarjeta con un texto y un botón: la carpeta de datos y las copias.
    void addCard(const QString &title, const QString &subtitle, const QString &action,
                 bool enabled, std::function<void(QWidget *)> clicked);

    Theme m_theme;
    const Store *m_store = nullptr;
    QVBoxLayout *m_layout = nullptr;   // contenido + stretch final
};
