#include "core/platform.hpp"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QIcon>
#include <QSettings>
#ifndef TAGOROR_MOBILE
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QSystemTrayIcon>
#endif

#include "ui/panel.hpp"
#ifdef TAGOROR_DEV_TOOLS
#include "dev/screenshottour.hpp"
#endif

// Set by CMake (see TAGOROR_APP_ID); a hand build gets the default.
#ifndef TAGOROR_APP_ID
#define TAGOROR_APP_ID "tagoror"
#endif

namespace {

#ifdef TAGOROR_LINUX_DESKTOP

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

#endif  // TAGOROR_LINUX_DESKTOP

}  // namespace

int main(int argc, char *argv[]) {
#ifdef TAGOROR_LINUX_DESKTOP
    choosePlatform();       // before QApplication: afterwards it cannot be chosen
#endif

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Stride");
    QCoreApplication::setApplicationName("Tagoror");
    // The update check compares against this.
    QCoreApplication::setApplicationVersion(TAGOROR_VERSION);

    // The typefaces come embedded: IBM Plex Sans for the interface and IBM Plex
    // Mono, the face the stylesheet asks for by name for times and counters. They are only
    // registered here: the Panel applies the one chosen in settings once it has
    // read them, and the system default must stay untouched for whoever picks it.
    const auto registerFamily = [&app](const char *property, const QStringList &files) {
        for (const QString &file : files) {
            const int id = QFontDatabase::addApplicationFont(":/fonts/" + file + ".ttf");
            if (app.property(property).toString().isEmpty() && id >= 0) {
                const QStringList families = QFontDatabase::applicationFontFamilies(id);
                if (!families.isEmpty()) app.setProperty(property, families.first());
            }
        }
    };
    registerFamily("bundledFont", {"IBMPlexSans-Regular", "IBMPlexSans-Italic",
                                   "IBMPlexSans-Medium", "IBMPlexSans-SemiBold",
                                   "IBMPlexSans-Bold", "IBMPlexSans-BoldItalic"});
    registerFamily("monoFont", {"IBMPlexMono-Regular", "IBMPlexMono-Medium",
                                "IBMPlexMono-SemiBold", "IBMPlexMono-Bold"});

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
#ifdef TAGOROR_MOBILE
    // On a phone the system already guarantees one instance, there is no tray to
    // hide in and the window is the whole screen.
    Panel panel;
    panel.showMaximized();
    panel.startUp(true);
#else
    if (mobileUi()) {
        // TAGOROR_FORCE_MOBILE=1: the phone layout in a window the size of a phone
        // (TAGOROR_MOBILE_SIZE=WxH, 412x915 by default). A preview, not a second
        // Tagoror: no single instance (it would hand over to the real one) and no
        // tray.
        QSize size(412, 915);
        const QStringList wh = qEnvironmentVariable("TAGOROR_MOBILE_SIZE").split('x');
        if (wh.size() == 2 && wh.at(0).toInt() > 0 && wh.at(1).toInt() > 0)
            size = QSize(wh.at(0).toInt(), wh.at(1).toInt());
        Panel panel;
        // As on a phone: the window is asked for the screen's size, and if the
        // layout's minimum is larger the widget grows past the screen, which a
        // phone then cuts (the screenshot tour crops to the screen to show it).
        panel.resize(size);
        panel.show();
#ifdef TAGOROR_DEV_TOOLS
        // scripts/mobile-shots.sh: photograph every page and quit. No startUp():
        // its splash and update check would get in the pictures.
        if (const QString dir = qEnvironmentVariable("TAGOROR_SHOTS_DIR"); !dir.isEmpty()) {
            new ScreenshotTour(&panel, dir);
            return app.exec();
        }
#endif
        panel.startUp(true);
        return app.exec();
    }

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

    // `--tray` starts hidden in the tray (for autostart), when there is one to
    // come back from; otherwise the window would be nowhere.
    const bool toTray = app.arguments().contains("--tray") && QSystemTrayIcon::isSystemTrayAvailable();
    if (!toTray) panel.show();
    panel.startUp(!toTray);
#endif
    return app.exec();
}
