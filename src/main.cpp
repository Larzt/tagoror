#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QSettings>

#include "ui/panel.hpp"

// Set by CMake (see TAGOROR_APP_ID); a hand build gets the default.
#ifndef TAGOROR_APP_ID
#define TAGOROR_APP_ID "tagoror"
#endif

namespace {

#ifdef Q_OS_LINUX

/// Under a Wayland session, asks for XWayland. On Wayland a window can neither
/// place itself nor know where it is (Qt always reports 0,0), and the widget
/// needs that to unfold from the dock towards free space.
///
/// Native Wayland can be chosen in settings (key "platform"), and
/// QT_QPA_PLATFORM in the environment overrides everything.
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
    choosePlatform();       // before QApplication: afterwards it cannot be chosen
#endif

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Stride");
    QCoreApplication::setApplicationName("Tagoror");
    // The update check compares against this.
    QCoreApplication::setApplicationVersion(TAGOROR_VERSION);

    // Links the window with its .desktop entry, where launchers and the window
    // switcher take its name and icon (essential on Wayland).
    QGuiApplication::setDesktopFileName(TAGOROR_APP_ID);
    // One PNG per size: small ones from the simplified icon, large ones from the
    // detailed one, which blurs below ~32 px.
    QIcon embedded;
    for (int size : {16, 24, 32, 48, 64, 128, 256})
        embedded.addFile(QString(":/icons/%1x%1/tagoror.png").arg(size), QSize(size, size));

    // The icon theme wins (so an icon pack can override); otherwise the embedded one.
    app.setWindowIcon(QIcon::fromTheme(TAGOROR_APP_ID, embedded));
    // The panel hides in the tray and comes back from there, so having no visible
    // window is no reason to quit. Without a tray, Panel::closeEvent quits itself.
    app.setQuitOnLastWindowClosed(false);

    // One instance per user: two panels over one notes.json means the last one to
    // save wins and the other's notes are lost.
    const QString key = QString("tagoror-%1").arg(qEnvironmentVariable("USER", "user"));
    // `listen` decides who is first, not a probe: binding the socket is atomic and
    // asking about it is not. At login, autostart and KDE session restore launch
    // both copies at once (measured), and an unconditional `removeServer` let the
    // loser delete the winner's socket, so both believed they were alone.
    QLocalServer server;

    if (!server.listen(key)) {
        // Either one is alive or a crash left the socket behind. The lock serialises
        // the check: two starting at once over an orphaned socket, the second waits
        // and finds the first already listening.
        QLockFile lock(QDir::tempPath() + "/" + key + ".lock");
        lock.setStaleLockTime(10000);
        lock.tryLock(3000);

        QLocalSocket probe;
        probe.connectToServer(key);
        if (probe.waitForConnected(200)) {
            probe.write("show");
            probe.waitForBytesWritten(200);
            return 0;                  // one is already open: let it come forward
        }

        QLocalServer::removeServer(key);   // orphaned socket: now it is safe
        server.listen(key);
    }

    Panel panel;

    // Connections arriving while the panel is being built wait in the socket's
    // queue; newConnection delivers them once the event loop runs.
    QObject::connect(&server, &QLocalServer::newConnection, &panel, [&server, &panel] {
        if (QLocalSocket *client = server.nextPendingConnection()) {
            client->deleteLater();
            panel.bringToFront();
        }
    });

    panel.show();
    return app.exec();
}
