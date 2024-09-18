#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>

class Session: public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.Session")

    Q_PROPERTY(uint version READ version CONSTANT)

    uint version() const;
public:
    explicit Session(const QDBusObjectPath &handle, const QVariant &data, QObject *parent = nullptr);
    ~Session() = default;
    QDBusObjectPath handle() const { return m_handle; }
    QVariant data() const { return m_data; }

public Q_SLOTS:
    void Close();
Q_SIGNALS:
    void Closed();
private:
    QDBusObjectPath m_handle;
    QVariant m_data;
};
