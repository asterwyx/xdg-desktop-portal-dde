// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screenshotframe.h"

#include "common.h"

Q_LOGGING_CATEGORY(portalWaylandProtocol, "dde.portal.wayland.protocol");

ScreenshotFrame::ScreenshotFrame(struct ::zwlr_screencopy_frame_v1 *object, QObject *parent)
    : QObject(parent)
    , QtWayland::zwlr_screencopy_frame_v1(object)
    , m_shmBuffer(nullptr)
    , m_pendingShmBuffer(nullptr)
    , m_flags()
{
}

void ScreenshotFrame::zwlr_screencopy_frame_v1_buffer(uint32_t format,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      uint32_t stride)
{
    // Create a new wl_buffer for reception
    // For some reason, Qt regards stride == width * 4, and it creates buffer likewise, we must
    // check this
    if (stride != width * 4) {
        qCDebug(portalWaylandProtocol)
                << "Receive a buffer format which is not compatible with QWaylandShmBuffer."
                << "format:" << format << "width:" << width << "height:" << height
                << "stride:" << stride;
        return;
    }
    if (m_pendingShmBuffer)
        return; // We only need one supported format
    m_pendingShmBuffer = new QtWaylandClient::QWaylandShmBuffer(
            waylandDisplay(),
            QSize(width, height),
            QtWaylandClient::QWaylandShm::formatFrom(static_cast<::wl_shm_format>(format)));
    copy(m_pendingShmBuffer->buffer());
}

void ScreenshotFrame::zwlr_screencopy_frame_v1_flags(uint32_t flags)
{
    m_flags = static_cast<QtWayland::zwlr_screencopy_frame_v1::flags>(flags);
}

void ScreenshotFrame::zwlr_screencopy_frame_v1_failed()
{
    Q_EMIT failed();
}

void ScreenshotFrame::zwlr_screencopy_frame_v1_ready(uint32_t tv_sec_hi,
                                                     uint32_t tv_sec_lo,
                                                     uint32_t tv_nsec)
{
    Q_UNUSED(tv_sec_hi);
    Q_UNUSED(tv_sec_lo);
    Q_UNUSED(tv_nsec);
    delete m_shmBuffer;
    m_shmBuffer = m_pendingShmBuffer;
    m_pendingShmBuffer = nullptr;
    Q_EMIT ready(*m_shmBuffer->image());
}

ScreenshotFrame::~ScreenshotFrame()
{
    destroy();
}
