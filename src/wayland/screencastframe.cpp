// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastframe.h"
#include "screencast.h"
#include <libdrm/drm_fourcc.h>
uint32_t xdpd_format_drm_fourcc_from_wl_shm(enum wl_shm_format format) {
    switch (format) {
    case WL_SHM_FORMAT_ARGB8888:
        return DRM_FORMAT_ARGB8888;
    case WL_SHM_FORMAT_XRGB8888:
        return DRM_FORMAT_XRGB8888;
    default:
        return (uint32_t)format;
    }
}
enum wl_shm_format xdpw_format_wl_shm_from_drm_fourcc(uint32_t format) {
    switch (format) {
    case DRM_FORMAT_ARGB8888:
        return WL_SHM_FORMAT_ARGB8888;
    case DRM_FORMAT_XRGB8888:
        return WL_SHM_FORMAT_XRGB8888;
    default:
        return (enum wl_shm_format)format;
    }
}

ScreenCastFrame::ScreenCastFrame(::zwlr_screencopy_frame_v1 *object,
                                 ScreenCast *cast,
                                 QObject *parent)
    : QObject(parent)
    , QtWayland::zwlr_screencopy_frame_v1(object)
    , m_cast(cast)
    , m_flags()
{
}

void ScreenCastFrame::zwlr_screencopy_frame_v1_buffer(uint32_t format,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      uint32_t stride)
{
    // Use the last buffer info.
    m_cast->screencopyFrameInfo[WL_SHM].format = xdpd_format_drm_fourcc_from_wl_shm(static_cast<wl_shm_format>(format));
    m_cast->screencopyFrameInfo[WL_SHM].width = width;
    m_cast->screencopyFrameInfo[WL_SHM].height = height;
    m_cast->screencopyFrameInfo[WL_SHM].stride = stride;
    m_cast->screencopyFrameInfo[WL_SHM].size = stride * height;
}

void ScreenCastFrame::zwlr_screencopy_frame_v1_flags(uint32_t flags)
{
    m_cast->currentFrame.yInverted = flags & QtWayland::zwlr_screencopy_frame_v1::flags_y_invert;
}

void ScreenCastFrame::zwlr_screencopy_frame_v1_ready(uint32_t tv_sec_hi,
                                                     uint32_t tv_sec_lo,
                                                     uint32_t tv_nsec)
{
    Q_UNUSED(tv_sec_hi);
    Q_UNUSED(tv_sec_lo);
    Q_UNUSED(tv_nsec);
}

void ScreenCastFrame::zwlr_screencopy_frame_v1_failed() { }

void ScreenCastFrame::zwlr_screencopy_frame_v1_damage(uint32_t x,
                                                      uint32_t y,
                                                      uint32_t width,
                                                      uint32_t height)
{
    m_cast->currentFrame.damages << XdpdFrameDamage{static_cast<int32_t>(x), static_cast<int32_t>(y), width, height};
}

void ScreenCastFrame::zwlr_screencopy_frame_v1_linux_dmabuf(uint32_t format,
                                                            uint32_t width,
                                                            uint32_t height)
{
    m_cast->screencopyFrameInfo[DMABUF].format = format;
    m_cast->screencopyFrameInfo[DMABUF].width = width;
    m_cast->screencopyFrameInfo[DMABUF].height = height;
}

void ScreenCastFrame::zwlr_screencopy_frame_v1_buffer_done()
{
    zwlr_screencopy_frame_v1::zwlr_screencopy_frame_v1_buffer_done();
}

void ScreenCastFrame::frame_finish()
{
    this->deleteLater();
    if (m_cast->quit || m_cast->err) {
        // TODO: revisit the exit condition (remove quit?)
        // and clean up sessions that still exist if err
        // is the cause of the instance_destroy call
        // TODO Delete screencast instance
        return;
    }
    if (!m_cast->pwrStreamState) {
        m_cast->frameState = XDPD_FRAME_STATE_NONE;
        return ;
    }

    if (m_cast->frameState == XDPD_FRAME_STATE_RENEG) {
        pwr_update_stream_param(m_cast);
    }

    if (m_cast->frameState == XDPD_FRAME_STATE_FAILED) {
        xdpd_pwr_enqueue_buffer(m_cast);
    }

    if (m_cast->frameState == XDPD_FRAME_STATE_SUCCESS) {
        xdpd_pwr_enqueue_buffer(m_cast);
    }
}
