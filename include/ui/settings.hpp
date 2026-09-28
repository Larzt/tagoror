#pragma once

#include <QByteArray>
#include <QColor>
#include <QWidget>

#include "core/drivesync.hpp"
#include "core/lang.hpp"
#include "core/store.hpp"
#include "ui/theme.hpp"

class ElidedLabel;
class QFrame;
class QToolButton;
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

    // Estado del buscador de actualizaciones. Se escribe encima de la tarjeta
    // ya montada en vez de rehacer la página: la comprobación diaria puede
    // terminar en cualquier momento, y si eso reconstruyera los ajustes se
    // llevaría por delante el deslizador que se estuviera arrastrando.
    void setUpdateState(bool busy, const QString &error);

    // La copia en Drive cambia de estado sola (una subida que termina, un
    // token que caduca): como con las actualizaciones, se reescribe la tarjeta
    // en su sitio. Solo si pasa de conectada a no conectada, o al revés, se
    // rehace la página, porque entonces cambia lo que hay debajo.
    void setDrive(const DriveSync *drive);
    void refreshDrive();

signals:
    void accentPicked(const QColor &c);
    void accentEditorRequested(QWidget *anchor);
    void opacityChanged(int value);          // en vivo, mientras se arrastra
    void languagePicked(Lang::Code code);
    void textScalePicked(int percent);
    void onTopToggled(bool on);
    void appModeToggled(bool on);
    void sizePerPageToggled(bool on);
    void x11Toggled(bool on);
    void dataFolderRequested();
    void backupsRequested(QWidget *anchor);
    void inputPicked(const QByteArray &id);
    void updateCheckToggled(bool on);
    void checkUpdatesRequested();
    void openLatestRequested();
    void quitRequested();
    void driveConnectRequested();
    void driveCancelRequested();
    void driveSyncRequested();
    void driveDisconnectRequested();
    void calendarToggled(bool on);
    void calendarGrantRequested();   // volver a autorizar con el permiso de Calendar
    void calendarFollowToggled(const QString &calId, bool on);
    void calendarTargetRequested(QWidget *anchor);

private:
    // Abre una sección: cabecera con icono y un grupo donde van sus filas.
    void beginGroup(const QString &title, const QString &icon);
    // Añade una fila al grupo abierto, con una línea encima si no es la primera.
    void addToGroup(QWidget *row);
    // Fila del grupo apilada en vertical, para los ajustes con mandos propios.
    QVBoxLayout *addBlock();
    void addAppearance();
    void addLanguage();
    void addWindow();
    void addData();
    void addInputs();
    void addUpdates();
    void addDrive();
    void addCalendar();
    // Lo que enseña la sección de Calendar: si cambia, se rehace la página.
    QString calendarSignature() const;
    void addQuit();
    // Lo que dice la tarjeta de actualizaciones ahora mismo.
    QString updateSubtitle(bool busy, const QString &error) const;
    // Fila con interruptor: rótulo a la izquierda, el mando a la derecha.
    void addToggle(const QString &label, const QString &hint, bool on,
                   std::function<void(bool)> changed);
    // Control segmentado: de sus opciones solo una está elegida.
    QWidget *segments(const QStringList &labels, int chosen, std::function<void(int)> picked,
                      QList<QToolButton *> *out = nullptr);
    // Fila con un texto y un botón: la carpeta de datos, las copias, Drive y
    // la versión. Devuelve el botón, y en subOut el subtítulo si se pide.
    QToolButton *addAction(const QString &title, const QString &subtitle,
                                 const QString &action, bool enabled,
                                 std::function<void(QWidget *)> clicked,
                                 ElidedLabel **subOut = nullptr);

    Theme m_theme;
    const Store *m_store = nullptr;
    QVBoxLayout *m_layout = nullptr;   // contenido + stretch final
    class QFrame *m_group = nullptr;   // el grupo de la sección que se está montando
    QVBoxLayout *m_groupLayout = nullptr;

    // De la tarjeta de actualizaciones, para reescribirla sin rehacer nada.
    class ElidedLabel *m_updateSub = nullptr;
    class QToolButton *m_updateBtn = nullptr;
    bool m_updateBusy = false;
    QString m_updateError;

    const DriveSync *m_drive = nullptr;
    class ElidedLabel *m_driveSub = nullptr;
    class QToolButton *m_driveBtn = nullptr;
    bool m_driveBuiltConnected = false;
    QString m_calendarBuilt;   // calendarSignature() con la que se montó
};
