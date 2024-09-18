// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <private/qwaylandclientextension_p.h>
#include <private/qwaylandshmbackingstore_p.h>
#include <qwayland-wlr-screencopy-unstable-v1.h>

#include <QList>
#include <QPointer>

class ScreenshotFrame : public QObject, public QtWayland::zwlr_screencopy_frame_v1
{
    Q_OBJECT
public:
    explicit ScreenshotFrame(struct ::zwlr_screencopy_frame_v1 *object, QObject *parent = nullptr);
    ~ScreenshotFrame() override;

    inline QtWayland::zwlr_screencopy_frame_v1::flags flags() { return m_flags; }

Q_SIGNALS:
    void ready(QImage image);
    void failed();

protected:
    void zwlr_screencopy_frame_v1_buffer(uint32_t format,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t stride) override;
    void zwlr_screencopy_frame_v1_flags(uint32_t flags) override;
    void zwlr_screencopy_frame_v1_ready(uint32_t tv_sec_hi,
                                        uint32_t tv_sec_lo,
                                        uint32_t tv_nsec) override;
    void zwlr_screencopy_frame_v1_failed() override;

private:
    QtWaylandClient::QWaylandShmBuffer *m_shmBuffer;
    QtWaylandClient::QWaylandShmBuffer *m_pendingShmBuffer;
    QtWayland::zwlr_screencopy_frame_v1::flags m_flags;
};