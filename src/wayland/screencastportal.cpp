// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastportal.h"

#include "request.h"
#include "screencast.h"
#include "screencastframe.h"
#include "screencastchooserdialog.h"
#include "screencasttargetmodel.h"

#include <pipewire/pipewire.h>
#include <spa/buffer/meta.h>
#include <spa/param/format-utils.h>
#include <spa/param/props.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/dynamic.h>
#include <spa/utils/result.h>


#include <QScreen>
#include <private/qwaylandscreen_p.h>
#include <QTimer>

#include <sys/mman.h>

Q_DECLARE_LOGGING_CATEGORY(qLcScreenCastPipewire)

static const struct pw_stream_events pwr_stream_events = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = pwr_handle_stream_state_changed,
    .param_changed = pwr_handle_stream_param_changed,
    .add_buffer = pwr_handle_stream_add_buffer,
    .remove_buffer = pwr_handle_stream_remove_buffer,
    .process = pwr_handle_stream_on_process,
};

ScreenCastPortalWayland::ScreenCastPortalWayland(PortalWaylandContext *context)
    : AbstractWaylandPortal(context)
    , m_state(new ScreenCastState)
    , m_context(new ScreenCastContext)
    , m_chooser(new ScreenCastChooserDialog(nullptr))
{
    m_chooser->QObject::setParent(this);
    if (!context->screenCopyManager()) {
        return;
    }
    auto screencast = ScreenCast::create("appId", context->screenCopyManager());
    createPipewireStream(screencast);
    // TODO node id is not ready here, ready after connected. Find a signal to notify portal.
    qCInfo(qLcScreenCastPipewire()) << "screen cast node id:" << screencast->nodeId;
}

uint ScreenCastPortalWayland::CreateSession(const QDBusObjectPath &handle,
                                     const QDBusObjectPath &sessionHandle,
                                     const QString &appId,
                                     const QVariantMap &options,
                                     QVariantMap &results)
{
    if (!context()->screenCopyManager()) {
        return XDPD_RESPONSE_CANCELLED;
    }
    auto screencast = ScreenCast::create(appId, context()->screenCopyManager());
    auto session = context()->createSession(sessionHandle, QVariant::fromValue(screencast));
    auto request = new Request(handle, QVariant::fromValue(session), this);
    // Use session path as session id.
    results.insert("session_id", QVariant::fromValue(sessionHandle.path()));
    return XDPD_RESPONSE_SUCCESS;
}

uint ScreenCastPortalWayland::SelectSources([[maybe_unused]] const QDBusObjectPath &handle,
                                     const QDBusObjectPath &sessionHandle,
                                     [[maybe_unused]] const QString &appId,
                                     const QVariantMap &options,
                                     [[maybe_unused]] QVariantMap &results)
{
    auto session = context()->findSession(sessionHandle);
    auto screencast = session->data().value<ScreenCast *>();
    if (!screencast)
        return XDPD_RESPONSE_CANCELLED;
    SourceTypes selectionTypes{Monitor};
    if (options.value("types").isValid()) {
        // Ignore requested types not available.
        selectionTypes = SourceTypes(options.value("types").toUInt() & availableSourceTypes().toInt());
    }
    if (!selectionTypes) {
        // No available type, just return.
        return XDPD_RESPONSE_CANCELLED;
    }
    m_allowMultipleSelection = options.value("multiple").isValid()
            ? options.value("multiple").toBool() : false;
    m_cursorMode = options.value("cursor_mode").isValid()
            ? CursorMode(options.value("cursor_mode").toUInt()) : Hidden;
    m_persistMode = options.value("persist_mode").isValid()
            ? PersistMode(options.value("persist_mode").toUInt()) : DoNotPersist;
    if (options.value("restore_data").isValid()) {
        // Handle restore data.
    }
    auto result = m_chooser->exec();
    if (result == QDialog::Rejected) {
        return XDPD_RESPONSE_CANCELLED;
    } else {
        // TODO set target
        auto view = m_chooser->listView();
        auto screen = qobject_cast<QScreen*>(reinterpret_cast<QObject*>(view->currentIndex().internalPointer()));
        auto waylandScreen = dynamic_cast<QtWaylandClient::QWaylandScreen *>(screen->handle());
        if (!waylandScreen) {
            qCWarning(qLcScreenCastPipewire()) << "Cannot get a wayland screen";
            return XDPD_RESPONSE_CANCELLED;
        }
        screencast->target.output = waylandScreen->output();
        screencast->target.with_cursor = m_cursorMode == Embedded;
    }
    return XDPD_RESPONSE_SUCCESS;
}

uint ScreenCastPortalWayland::Start([[maybe_unused]] const QDBusObjectPath &handle,
                             const QDBusObjectPath &sessionHandle,
                             [[maybe_unused]] const QString &app_id,
                             [[maybe_unused]] const QString &parent_window,
                             const QVariantMap &options,
                             QVariantMap &results)
{
    auto session = context()->findSession(sessionHandle);
    auto screencast = session->data().value<ScreenCast *>();
    if (!screencast)
        return XDPD_RESPONSE_CANCELLED;
    createPipewireStream(screencast);
    results.insert("persist_mode", QVariant::fromValue(m_persistMode));
    return screencast->nodeId;
}

uint32_t ScreenCastPortalWayland::createPipewireStream(ScreenCast *cast)
{
    pw_loop_enter(context()->pipewireLoop());
    uint8_t buffer[2][1024];
    struct spa_pod_dynamic_builder b[2];
    spa_pod_dynamic_builder_init(&b[0], buffer[0], sizeof(buffer[0]), 2048);
    spa_pod_dynamic_builder_init(&b[1], buffer[1], sizeof(buffer[1]), 2048);
    const struct spa_pod *params[2];

    QString name = QString("xdpd-stream-%1").arg(cast->castId);

    cast->stream = pw_stream_new(context()->pipewireCore(),
                                 name.toStdString().c_str(),
                                 pw_properties_new(PW_KEY_MEDIA_CLASS, "Video/Source", nullptr));

    if (!cast->stream) {
        qCCritical(qLcScreenCastPipewire) << "Failed to create stream.";
        return 0;
    }
    cast->pwrStreamState = false;

    struct spa_pod_builder *builder[2] = { &b[0].b, &b[1].b };
    uint32_t param_count = build_formats(builder, cast, params);
    spa_pod_dynamic_builder_clean(&b[0]);
    spa_pod_dynamic_builder_clean(&b[1]);

    pw_stream_add_listener(cast->stream, &cast->streamListener, &pwr_stream_events, cast);

    pw_stream_connect(cast->stream,
                      PW_DIRECTION_OUTPUT,
                      PW_ID_ANY,
                      PW_STREAM_FLAG_ALLOC_BUFFERS,
                      params,
                      param_count);
    cast->nodeId = pw_stream_get_node_id(cast->stream);
    return cast->nodeId;
}
