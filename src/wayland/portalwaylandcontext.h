// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "screencopymanager.h"
#include "screenshotframe.h"
#include "session.h"
#include "treelandcapture.h"

#include <pipewire/context.h>
#include <pipewire/loop.h>
#include <pipewire/pipewire.h>
#include <private/qwaylanddisplay_p.h>

#include <QDBusContext>
#include <QDBusObjectPath>
#include <QSocketDescriptor>
struct ScreenCastContext;
struct ScreenCastState;
struct ScreenCast;
class ScreenCastChooserDialog;

static constexpr int XDPD_RESPONSE_SUCCESS = 0;
static constexpr int XDPD_RESPONSE_CANCELLED = 1;
static constexpr int XDPD_RESPONSE_ENDED = 2;

class PortalWaylandContext : public QObject, public QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(uint AvailableSourceTypes READ availableSourceTypes)
    Q_PROPERTY(uint AvailableCursorModes READ availableCursorModes)
    Q_PROPERTY(uint version READ version)
    static constexpr uint SCREEN_CAST_API_VERSION = 1;
    static constexpr uint SCREENSHOT_API_VERSION = 1;

public:
    enum SourceType { Monitor = 0x1, Window = 0x2, Virtual = 0x4 };
    Q_FLAG(SourceType)
    Q_DECLARE_FLAGS(SourceTypes, SourceType)

    enum CursorMode { Hidden = 0x1, Embedded = 0x2, Metadata = 0x4 };
    Q_FLAG(CursorMode)
    Q_DECLARE_FLAGS(CursorModes, CursorMode)

    enum PersistMode { DoNotPersist, WhenAppRunning, UntilExplicitlyRevoked };
    Q_ENUM(PersistMode)
    explicit PortalWaylandContext(QObject *parent = nullptr);

    inline QPointer<ScreenCopyManager> screenCopyManager() { return m_screenCopyManager; }

    inline QPointer<TreeLandCaptureManager> treelandCaptureManager()
    {
        return m_treelandCaptureManager;
    }

    Session *createSession(const QDBusObjectPath &handle, const QVariant &data);
    void destroySession(Session *session);
    Session *findSession(const QDBusObjectPath &handle);
    [[nodiscard]] pw_loop *pipewireLoop() const;
    [[nodiscard]] pw_core *pipewireCore() const;
    [[nodiscard]] pw_context *pipewireContext() const;

    [[nodiscard]] SourceTypes availableSourceTypes() const { qInfo() <<" Get source type"; return Monitor; }

    [[nodiscard]] CursorModes availableCursorModes() const { return { Hidden | Embedded }; }

    [[nodiscard]] uint version() const { return SCREEN_CAST_API_VERSION; }

    void initializePipewireContext();
    void handlePipewireRead(QSocketDescriptor socket, QSocketNotifier::Type type);

Q_SIGNALS:
    void pipewireIOError(int errorCode);

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
    uint32_t createPipewireStream(ScreenCast *cast) const;

    ScreenCopyManager *m_screenCopyManager;
    TreeLandCaptureManager *m_treelandCaptureManager;
    pw_loop *m_pipewireLoop;
    QList<Session *> m_sessions;
    QSocketNotifier *m_pipewireSocket;
    pw_context *m_pipewireContext;
    pw_core *m_pipewireCore;
    SourceType m_sourceType{};
    CursorMode m_cursorMode{};
    PersistMode m_persistMode{};
    bool m_allowMultipleSelection{};
    ScreenCastState *m_state{};
    ScreenCastContext *m_context{};
    ScreenCastChooserDialog *m_chooser{};
};
