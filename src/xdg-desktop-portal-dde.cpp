// SPDX-FileCopyrightText: 2021 - 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "ddesktopportal.h"
#include "wayland/portalwaylandcontext.h"

#include <pipewire/pipewire.h>
#include <qstringliteral.h>

#include <QApplication>
#include <QDBusConnection>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(XdgDesktopDDE, "xdg-dde")

inline static bool onWayland()
{
    return QGuiApplication::platformName() == "wayland";
}

int main(int argc, char *argv[])
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    /* Disable X11 session management
    As XdgDesktopPortal is a QGuiApplication it connects to our X session manager.
    By default, Qt apps behave like "applications" and this can end up on our auto-restart list if the user saves their session.
    */
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);
#endif

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication a(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    pw_init(&argc, &argv);

    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (sessionBus.registerService(QStringLiteral("org.freedesktop.impl.portal.desktop.dde"))) {
        if (onWayland()) {
            auto waylandContext = new PortalWaylandContext(&a);
            if (sessionBus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"),
                                          waylandContext,
                                          QDBusConnection::ExportAdaptors)) {
                qCDebug(XdgDesktopDDE) << "portal started on wayland";
            }
            auto conn = QObject::connect(waylandContext, &PortalWaylandContext::pipewireIOError, &a, &QApplication::exit);
            if (!conn) {
                qCWarning(XdgDesktopDDE) << "IO error handler not connected, may not exit normally.";
            }
        } else {
            auto desktopPortal = new DDesktopPortal(&a);
            if (sessionBus.registerObject(QStringLiteral("/org/freedesktop/portal/desktop"),
                                          desktopPortal,
                                          QDBusConnection::ExportAdaptors)) {
                qCDebug(XdgDesktopDDE) << "portal started on x11";
            }
        }

    } else {
        qCDebug(XdgDesktopDDE) << "Another portal is starting";
        return 1;
    }


    return a.exec();
}
