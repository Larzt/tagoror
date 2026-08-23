#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>

#include "ui/panel.hpp"

// Lo fija CMake (ver TAGOROR_APP_ID); compilando a mano vale el de siempre.
#ifndef TAGOROR_APP_ID
#define TAGOROR_APP_ID "tagoror"
#endif

namespace {

#ifdef Q_OS_LINUX

// En Wayland una ventana no puede colocarse a sí misma ni saber dónde está:
// Qt informa siempre de 0,0 (comprobado). El widget necesita justamente eso
// para desplegarse desde el dock hacia el centro de la pantalla en vez de
// hacia la derecha siempre, así que bajo una sesión Wayland se pide XWayland.
//
// Se puede volver a Wayland nativo desde ajustes (clave 'platform') o fijando
// QT_QPA_PLATFORM en el entorno, que manda sobre todo lo demás.
void choosePlatform() {
    if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) return;

    const QString choice = QSettings("Stride", "Tagoror").value("platform").toString();
    if (choice == "wayland") return;
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland")
        qputenv("QT_QPA_PLATFORM", "xcb");
}

#endif  // Q_OS_LINUX

}  // namespace

int main(int argc, char *argv[]) {
#ifdef Q_OS_LINUX
    choosePlatform();       // antes de QApplication: después ya no se elige
#endif

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Stride");
    QCoreApplication::setApplicationName("Tagoror");

    // Enlaza la ventana con su entrada .desktop: de ahí sacan el nombre y el
    // icono el lanzador y el conmutador de ventanas (imprescindible en Wayland).
    QGuiApplication::setDesktopFileName(TAGOROR_APP_ID);
    // Un PNG por tamaño: los pequeños salen del icono simplificado y los
    // grandes del detallado, que por debajo de ~32 px se emborrona.
    QIcon embedded;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        embedded.addFile(QString(":/icons/%1x%1/tagoror.png").arg(size), QSize(size, size));

    // El tema manda (así se puede sustituir con un icon pack); si no lo
    // resuelve, el que va dentro del binario.
    app.setWindowIcon(QIcon::fromTheme(TAGOROR_APP_ID, embedded));
    // El panel se esconde en la bandeja y desde ahí se recupera, así que
    // quedarse sin ventana visible no es motivo para terminar: de salir se
    // encarga "Salir", en ajustes o en el menú de la bandeja. Sin bandeja
    // disponible, Panel::closeEvent sale por su cuenta.
    app.setQuitOnLastWindowClosed(false);

    // Una sola instancia por usuario. Lanzándolo desde el menú con el widget
    // ya abierto (por ejemplo por el autoarranque) saldrían dos paneles sobre
    // el mismo notes.json y el último en guardar se comería las notas del otro.
    const QString key = QString("tagoror-%1").arg(qEnvironmentVariable("USER", "user"));

    QLocalSocket probe;
    probe.connectToServer(key);
    if (probe.waitForConnected(200)) {
        probe.write("show");
        probe.waitForBytesWritten(200);
        return 0;                      // ya hay una abierta: que dé la cara ella
    }

    Panel panel;

    QLocalServer server;
    QLocalServer::removeServer(key);   // socket huérfano de un cierre brusco
    server.listen(key);
    QObject::connect(&server, &QLocalServer::newConnection, &panel, [&server, &panel] {
        if (QLocalSocket *client = server.nextPendingConnection()) {
            client->deleteLater();
            panel.bringToFront();
        }
    });

    panel.show();
    return app.exec();
}
