#include "session.h"

#include <QDBusMessage>
#include <QDBusConnection>
#define XDPD_SESSION_VERSION 1
Session::Session(const QDBusObjectPath &handle, const QVariant &data, QObject *parent) : QDBusAbstractAdaptor(parent) {}

uint Session::version() const { return XDPD_SESSION_VERSION;}

void Session::Close()
{
    Q_EMIT Closed();
    deleteLater();
}
