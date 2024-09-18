// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "fpslimit.h"
#include "linuxdmabuf.h"
#include "screencastframe.h"
#include "screencopymanager.h"
#include "screenshotframe.h"

#include <gbm.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>

#include <QHash>
#include <QObject>


#define XDPW_PWR_BUFFERS 2
#define XDPW_PWR_BUFFERS_MIN 2
#define XDPW_PWR_ALIGN 16


struct ScreenCopyFrameInfo
{
    uint32_t width;
    uint32_t height;
    uint32_t size;
    uint32_t stride;
    uint32_t format;
};

struct FormatModifierPair
{
    uint32_t fourcc;
    uint64_t modifier;
};

struct XdpdFrameDamage
{
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

inline XdpdFrameDamage mergeDamage(struct XdpdFrameDamage *damage1, struct XdpdFrameDamage *damage2)
{
    struct XdpdFrameDamage damage{};
    uint32_t x0, y0;
    damage.x = damage1->x < damage2->x ? damage1->x : damage2->x;
    damage.y = damage1->y < damage2->y ? damage1->y : damage2->y;

    x0 = damage1->x + damage1->width < damage2->x + damage2->width ? damage2->x + damage2->width
                                                                   : damage1->x + damage1->width;
    y0 = damage1->y + damage1->height < damage2->y + damage2->height ? damage2->y + damage2->height
                                                                     : damage1->y + damage1->height;
    damage.width = x0 - damage.x;
    damage.height = y0 - damage.y;

    return damage;
}

enum BufferType { WL_SHM, DMABUF, BufferTypeCount };

struct XdpdBuffer
{
    struct wl_list link;
    enum BufferType buffer_type;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    int plane_count;
    int fd[4];
    uint32_t size[4];
    uint32_t stride[4];
    uint32_t offset[4];
    struct gbm_bo *bo;
    struct wl_buffer *buffer;
};

XdpdBuffer *xdpdBufferCreate(ScreenCast *cast,
                             BufferType buffer_type,
                             ScreenCopyFrameInfo *frame_info);

void xdpdBufferDestroy(XdpdBuffer *buffer);

struct XdpdFrame
{
    bool yInverted;
    uint64_t tv_sec;
    uint32_t tv_nsec;
    uint32_t transformation;
    QList<XdpdFrameDamage> damages;
    uint32_t damageCount;
    struct XdpdBuffer *xdpdBuffer;
    struct pw_buffer *pwBuffer;
};

enum xdpd_frame_state {
    XDPD_FRAME_STATE_NONE,
    XDPD_FRAME_STATE_STARTED,
    XDPD_FRAME_STATE_RENEG,
    XDPD_FRAME_STATE_FAILED,
    XDPD_FRAME_STATE_SUCCESS,
};

enum ScreenCastChooserType {
    SC_CHOOSER_DEFAULT,
    SC_CHOOSER_NONE,
    SC_CHOOSER_SIMPLE,
    SC_CHOOSER_DMENU,
};

struct ScreenCastConfig
{
    QString outputName;
    double maxFps;
    QString execBefore;
    QString execAfter;
    QString chooserCmd;
    ScreenCastChooserType chooserType;
    bool forceModLinear = false;
};

struct ScreenCastState
{
    pw_loop *pwLoop{};
    ScreenCastConfig config;
    int timerPollFd{};
    QTimer *nextTimer{};
    QList<QTimer *> timers;
};

struct ScreenCastContext
{
    ScreenCastState *state{};
    wl_shm *shm{};
    gbm_device *gbm{ nullptr };
    LinuxDmabuf *dmabuf{};
    ScreenCopyManager *screencopyManager{};
};

struct ScreenCastTarget
{
    union {
        struct
        {
            ::wl_output *output;
            bool with_cursor;
        };
    };
};

struct ScreenCast
{
    QString appId{};
    QString castId{};
    pw_stream *stream{ nullptr };
    bool pwrStreamState{ false };
    bool avoidDmabufs{ false };
    ScreenCopyFrameInfo screencopyFrameInfo[BufferTypeCount];
    uint32_t framerate;
    QHash<uint32_t, uint64_t> formatModifierPairs;

    spa_hook streamListener;
    uint32_t nodeId;
    XdpdFrame currentFrame;
    xdpd_frame_state frameState;
    int err;
    bool quit;
    bool initialized;
    uint32_t seq;
    BufferType bufferType;
    spa_video_info_raw pwrFormat;
    ScreenCastContext *ctx;
    QList<XdpdBuffer *> bufferList;
    fps_limit_state fps_limit;
    ScreenCastFrame *castFrame;
    ScreenCastTarget target;

    static ScreenCast *create(const QString &appId, ScreenCopyManager *manager);
};
spa_pod *build_format(struct spa_pod_builder *b,
                      enum spa_video_format format,
                      uint32_t width,
                      uint32_t height,
                      uint32_t framerate,
                      uint64_t *modifiers,
                      int modifier_count);
uint32_t build_formats(struct spa_pod_builder *b[2],
                       ScreenCast *cast,
                       const struct spa_pod *params[2]);
bool build_modifier_list(ScreenCast *cast,
                                uint32_t drm_format,
                                uint64_t **modifiers,
                                uint32_t *modifier_count);
void xdpd_pwr_dequeue_buffer(ScreenCast *cast);
void xdpd_pwr_enqueue_buffer(ScreenCast *cast);
void pwr_update_stream_param(ScreenCast *cast);
void pwr_handle_stream_remove_buffer(void *data, struct pw_buffer *buffer);
void pwr_handle_stream_on_process(void *data);
void pwr_handle_stream_add_buffer(void *data, struct pw_buffer *buffer);
void pwr_handle_stream_state_changed(void *data,
                                            enum pw_stream_state old,
                                            enum pw_stream_state state,
                                            [[maybe_unused]] const char *error);
void pwr_handle_stream_param_changed(void *data, uint32_t id, const struct spa_pod *param);
spa_pod *fixate_format(struct spa_pod_builder *b,
                              enum spa_video_format format,
                              uint32_t width,
                              uint32_t height,
                              uint32_t framerate,
                              const uint64_t *modifier);