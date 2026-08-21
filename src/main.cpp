#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>

#include "ui/panel.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Stride");
    QCoreApplication::setApplicationName("Codex");

    // Enlaza la ventana con su entrada .desktop: de ahí sacan el nombre y el
    // icono el lanzador y el conmutador de ventanas (imprescindible en Wayland).
    QGuiApplication::setDesktopFileName("codex");
    // Un PNG por tamaño: los pequeños salen del icono simplificado y los
    // grandes del detallado, que por debajo de ~32 px se emborrona.
    QIcon embedded;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        embedded.addFile(QString(":/icons/%1x%1/codex.png").arg(size), QSize(size, size));

    // El tema manda (así se puede sustituir con un icon pack); si no lo
    // resuelve, el que va dentro del binario.
    app.setWindowIcon(QIcon::fromTheme("codex", embedded));
    app.setQuitOnLastWindowClosed(true);

    // Una sola instancia por usuario. Lanzándolo desde el menú con el widget
    // ya abierto (por ejemplo por el autoarranque) saldrían dos paneles sobre
    // el mismo notes.json y el último en guardar se comería las notas del otro.
    const QString key = QString("codex-%1").arg(qEnvironmentVariable("USER", "user"));

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
