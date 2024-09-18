// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include "qwayland-wlr-screencopy-unstable-v1.h"

#include <QObject>

struct ScreenCast;

class ScreenCastFrame : public QObject, public QtWayland::zwlr_screencopy_frame_v1
{
    Q_OBJECT
public:
    ScreenCastFrame(struct ::zwlr_screencopy_frame_v1 *object,
                    ScreenCast *cast,
                    QObject *parent = nullptr);

Q_SIGNALS:
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
    void zwlr_screencopy_frame_v1_damage(uint32_t x,
                                         uint32_t y,
                                         uint32_t width,
                                         uint32_t height) override;
    void zwlr_screencopy_frame_v1_linux_dmabuf(uint32_t format,
                                               uint32_t width,
                                               uint32_t height) override;
    void zwlr_screencopy_frame_v1_buffer_done() override;

private:
    void frame_finish();

    QtWayland::zwlr_screencopy_frame_v1::flags m_flags;
    ScreenCast *const m_cast;
};
