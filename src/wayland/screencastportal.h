#pragma clang diagnostic push
#pragma ide diagnostic ignored "readability-convert-member-functions-to-static"
// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include "abstractwaylandportal.h"

#include <pipewire/pipewire.h>

#include <QDBusObjectPath>
struct ScreenCastContext;
struct ScreenCastState;
struct ScreenCast;
class ScreenCastChooserDialog;

class ScreenCastPortalWayland : public AbstractWaylandPortal
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.impl.portal.ScreenCast")
    Q_CLASSINFO("D-Bus Introspection",
                "<?xml version=\" 1.0 \"?>"
                "<node name=\"\" "
                "xmlns:doc=\"http://www.freedesktop.org/dbus/1.0/doc.dtd\">\n"
                "<interface name=\"org.freedesktop.impl.portal.ScreenCast\">\n"
                "<method name=\"CreateSession\">\n"
                "<arg type=\"o\" name=\"handle\" direction=\"in\"/>\n"
                "<arg type=\"o\" name=\"session_handle\" direction=\"in\"/>\n"
                "<arg type=\"s\" name=\"app_id\" direction=\"in\"/>\n"
                "<annotation name=\"org.qtproject.QtDBus.QtTypeName.In3\" "
                "value=\"QVariantMap\"/>\n"
                "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>\n"
                "<arg type=\"u\" name=\"response\" direction=\"out\"/>\n"
                "<annotation name=\"org.qtproject.QtDBus.QtTypeName.Out1\" "
                "value=\"QVariantMap\"/>\n"
                "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>\n"
                "</method>\n"
                "<method name=\"SelectSources\">\n"
                "<arg type=\"o\" name=\"handle\" direction=\"in\"/>\n"
                "<arg type=\"o\" name=\"session_handle\" direction=\"in\"/>\n"
                "<arg type=\"s\" name=\"app_id\" direction=\"in\"/>\n"
                "<annotation name=\"org.qtproject.QtDBus.QtTypeName.In3\" "
                "value=\"QVariantMap\"/>\n"
                "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>\n"
                "<arg type=\"u\" name=\"response\" direction=\"out\"/>\n"
                "<annotation name=\"org.qtproject.QtDBus.QtTypeName.Out1\" "
                "value=\"QVariantMap\"/>\n"
                "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>\n"
                "</method>\n"
                "<method name=\"Start\">\n"
                "<arg type=\"o\" name=\"handle\" direction=\"in\"/>\n"
                "<arg type=\"o\" name=\"session_handle\" direction=\"in\"/>\n"
                "<arg type=\"s\" name=\"app_id\" direction=\"in\"/>\n"
                "<arg type=\"s\" name=\"parent_window\" direction=\"in\"/>\n"
                "<annotation name=\"org.qtproject.QtDBus.QtTypeName.In4\" "
                "value=\"QVariantMap\"/>\n"
                "<arg type=\"a{sv}\" name=\"options\" direction=\"in\"/>\n"
                "<arg type=\"u\" name=\"response\" direction=\"out\"/>\n"
                "<annotation name=\"org.qtproject.QtDBus.QtTypeName.Out1\" "
                "value=\"QVariantMap\"/>\n"
                "<arg type=\"a{sv}\" name=\"results\" direction=\"out\"/>\n"
                "</method>\n"
                "<property name=\"AvailableSourceTypes\" type=\"u\" "
                "access=\"read\"/>\n"
                "<property name=\"AvailableCursorModes\" type=\"u\" "
                "access=\"read\"/>\n"
                "<property name=\"version\" type=\"u\" access=\"read\"/>\n"
                "</interface>\n"
                "</node>\n")
    Q_PROPERTY(uint AvailableSourceTypes READ availableSourceTypes)
    Q_PROPERTY(uint AvailableCursorModes READ availableCursorModes)
    Q_PROPERTY(uint version READ version)
public:
    static constexpr uint SCREEN_CAST_API_VERSION = 1;
    explicit ScreenCastPortalWayland(PortalWaylandContext *context);

    enum SourceType { Monitor = 0x1, Window = 0x2, Virtual = 0x4 };
    Q_FLAG(SourceType)
    Q_DECLARE_FLAGS(SourceTypes, SourceType)

    enum CursorMode { Hidden = 0x1, Embedded = 0x2, Metadata = 0x4 };
    Q_FLAG(CursorMode)
    Q_DECLARE_FLAGS(CursorModes, CursorMode)

    enum PersistMode {
        DoNotPersist,
        WhenAppRunning,
        UntilExplicitlyRevoked
    };
    Q_ENUM(PersistMode)

    [[nodiscard]] SourceTypes availableSourceTypes() { return Monitor; }
    [[nodiscard]] CursorModes availableCursorModes() { return {Hidden & Embedded}; }
    [[nodiscard]] uint version() const { return SCREEN_CAST_API_VERSION; }
    ~ScreenCastPortalWayland() override = default;

public Q_SLOTS:

    uint CreateSession(const QDBusObjectPath &handle,
                       const QDBusObjectPath &session_handle,
                       const QString &app_id,
                       const QVariantMap &options,
                       QVariantMap &results);

    uint SelectSources(const QDBusObjectPath &handle,
                       const QDBusObjectPath &session_handle,
                       const QString &app_id,
                       const QVariantMap &options,
                       QVariantMap &results);

    uint Start(const QDBusObjectPath &handle,
               const QDBusObjectPath &session_handle,
               const QString &app_id,
               const QString &parent_window,
               const QVariantMap &options,
               QVariantMap &results);

private:
    uint32_t createPipewireStream(ScreenCast *cast);


    SourceType m_sourceType{};
    CursorMode m_cursorMode{};
    PersistMode m_persistMode{};
    bool m_allowMultipleSelection{};
    uint m_version{};
    pw_loop *m_loop{};
    pw_core *m_core{};
    ScreenCastState *m_state{};
    ScreenCastContext *m_context{};
    ScreenCastChooserDialog *m_chooser{};
};

#pragma clang diagnostic pop