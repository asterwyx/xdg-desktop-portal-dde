// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencast.h"

#include <libdrm/drm_fourcc.h>
#include <pipewire/pipewire.h>
#include <spa/buffer/meta.h>
#include <spa/param/format-utils.h>
#include <spa/param/props.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/dynamic.h>
#include <spa/utils/result.h>

#include <QDateTime>
#include <QTimer>

#include <fcntl.h>
#include <sys/mman.h>

Q_DECLARE_LOGGING_CATEGORY(qLcScreenCastPipewire)

ScreenCast *ScreenCast::create(const QString &appId, ScreenCopyManager *manager)
{
    auto sc = new ScreenCast;
    sc->appId = appId;
    sc->castId = appId + QStringLiteral("-%1").arg(QDateTime::currentMSecsSinceEpoch());
    return sc;
}

static int anonymous_shm_open(const QString &appId)
{
    QString name{ "/xdpd-shm-" + appId };
    int retries = 100;

    do {
        --retries;
        // shm_open guarantees that O_CLOEXEC is set
        int fd = shm_open(name.toStdString().c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd >= 0) {
            shm_unlink(name.toStdString().c_str());
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

static struct wl_buffer *import_wl_shm_buffer(
        ScreenCast *cast, int fd, enum wl_shm_format fmt, int width, int height, int stride)
{
    int size = stride * height;

    if (fd < 0) {
        return nullptr;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(cast->ctx->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, fmt);
    wl_shm_pool_destroy(pool);

    return buffer;
}

enum wl_shm_format xdpd_format_wl_shm_from_drm_fourcc(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_ARGB8888:
        return WL_SHM_FORMAT_ARGB8888;
    case DRM_FORMAT_XRGB8888:
        return WL_SHM_FORMAT_XRGB8888;
    default:
        return (enum wl_shm_format)format;
    }
}

struct XdpdBuffer *xdpdBufferCreate(struct ScreenCast *cast,
                                    enum BufferType buffer_type,
                                    struct ScreenCopyFrameInfo *frame_info)
{
    auto buffer = new XdpdBuffer;
    buffer->width = frame_info->width;
    buffer->height = frame_info->height;
    buffer->format = frame_info->format;
    buffer->buffer_type = buffer_type;

    switch (buffer_type) {
    case WL_SHM: {
        buffer->plane_count = 1;
        buffer->size[0] = frame_info->size;
        buffer->stride[0] = frame_info->stride;
        buffer->offset[0] = 0;
        buffer->fd[0] = anonymous_shm_open(cast->appId);
        if (buffer->fd[0] == -1) {
            qCritical() << "unable to create anonymous file descriptor";
            free(buffer);
            return nullptr;
        }

        if (ftruncate(buffer->fd[0], buffer->size[0]) < 0) {
            qCritical() << "unable to truncate file descriptor";
            close(buffer->fd[0]);
            free(buffer);
            return nullptr;
        }

        buffer->buffer =
                import_wl_shm_buffer(cast,
                                     buffer->fd[0],
                                     xdpd_format_wl_shm_from_drm_fourcc(frame_info->format),
                                     frame_info->width,
                                     frame_info->height,
                                     frame_info->stride);
        if (buffer->buffer == nullptr) {
            qCritical() << "unable to create wl_buffer";
            close(buffer->fd[0]);
            free(buffer);
            return nullptr;
        }
        break;
    }
    case DMABUF: {
        uint32_t flags = GBM_BO_USE_RENDERING;
        if (cast->pwrFormat.modifier != DRM_FORMAT_MOD_INVALID) {
            uint64_t *modifiers = (uint64_t *)&cast->pwrFormat.modifier;
            buffer->bo = gbm_bo_create_with_modifiers2(cast->ctx->gbm,
                                                       frame_info->width,
                                                       frame_info->height,
                                                       frame_info->format,
                                                       modifiers,
                                                       1,
                                                       flags);
        } else {
            if (cast->ctx->state->config.forceModLinear) {
                flags |= GBM_BO_USE_LINEAR;
            }
            buffer->bo = gbm_bo_create(cast->ctx->gbm,
                                       frame_info->width,
                                       frame_info->height,
                                       frame_info->format,
                                       flags);
        }

        // Fallback for linear buffers via the implicit api
        if (buffer->bo == nullptr && cast->pwrFormat.modifier == DRM_FORMAT_MOD_LINEAR) {
            buffer->bo = gbm_bo_create(cast->ctx->gbm,
                                       frame_info->width,
                                       frame_info->height,
                                       frame_info->format,
                                       flags | GBM_BO_USE_LINEAR);
        }

        if (buffer->bo == nullptr) {
            qCritical() << "failed to create gbm_bo";
            free(buffer);
            return nullptr;
        }
        buffer->plane_count = gbm_bo_get_plane_count(buffer->bo);

        // struct zwp_linux_buffer_params_v1 *params;
        auto params = cast->ctx->dmabuf->create_params();
        if (!params) {
            qCritical() << "failed to create linux_buffer_params";
            gbm_bo_destroy(buffer->bo);
            free(buffer);
            return nullptr;
        }

        for (int plane = 0; plane < buffer->plane_count; plane++) {
            buffer->size[plane] = 0;
            buffer->stride[plane] = gbm_bo_get_stride_for_plane(buffer->bo, plane);
            buffer->offset[plane] = gbm_bo_get_offset(buffer->bo, plane);
            uint64_t mod = gbm_bo_get_modifier(buffer->bo);
            buffer->fd[plane] = gbm_bo_get_fd_for_plane(buffer->bo, plane);

            if (buffer->fd[plane] < 0) {
                qCritical() << "failed to get file descriptor";
                zwp_linux_buffer_params_v1_destroy(params);
                gbm_bo_destroy(buffer->bo);
                for (int plane_tmp = 0; plane_tmp < plane; plane_tmp++) {
                    close(buffer->fd[plane_tmp]);
                }
                free(buffer);
                return nullptr;
            }
            zwp_linux_buffer_params_v1_add(params,
                                           buffer->fd[plane],
                                           plane,
                                           buffer->offset[plane],
                                           buffer->stride[plane],
                                           mod >> 32,
                                           mod & 0xffffffff);
        }
        buffer->buffer = zwp_linux_buffer_params_v1_create_immed(params,
                                                                 buffer->width,
                                                                 buffer->height,
                                                                 buffer->format,
                                                                 /* flags */ 0);
        zwp_linux_buffer_params_v1_destroy(params);

        if (!buffer->buffer) {
            qCritical() << "failed to create buffer";
            gbm_bo_destroy(buffer->bo);
            for (int plane = 0; plane < buffer->plane_count; plane++) {
                close(buffer->fd[plane]);
            }
            free(buffer);
            return nullptr;
        }
        break;
    }
    case BufferTypeCount:
        qCCritical(qLcScreenCastPipewire)
                << "BufferTypeCount is used for type count, not a meaningful enumeration.";
        return nullptr;
    }
    return buffer;
}

void xdpdBufferDestroy(XdpdBuffer *buffer)
{
    wl_buffer_destroy(buffer->buffer);
    if (buffer->buffer_type == DMABUF) {
        gbm_bo_destroy(buffer->bo);
    }
    for (int plane = 0; plane < buffer->plane_count; plane++) {
        close(buffer->fd[plane]);
    }
    wl_list_remove(&buffer->link);
    free(buffer);
}

static struct spa_pod *build_buffer(struct spa_pod_builder *b,
                                    uint32_t blocks,
                                    uint32_t size,
                                    uint32_t stride,
                                    uint32_t datatype)
{
    Q_ASSERT(blocks > 0);
    Q_ASSERT(datatype > 0);
    struct spa_pod_frame f[1];

    spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers);
    spa_pod_builder_add(b,
                        SPA_PARAM_BUFFERS_buffers,
                        SPA_POD_CHOICE_RANGE_Int(XDPW_PWR_BUFFERS, XDPW_PWR_BUFFERS_MIN, 32),
                        0);
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(blocks), 0);
    if (size > 0) {
        spa_pod_builder_add(b, SPA_PARAM_BUFFERS_size, SPA_POD_Int(size), 0);
    }
    if (stride > 0) {
        spa_pod_builder_add(b, SPA_PARAM_BUFFERS_stride, SPA_POD_Int(stride), 0);
    }
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_align, SPA_POD_Int(XDPW_PWR_ALIGN), 0);
    spa_pod_builder_add(b, SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(datatype), 0);
    return static_cast<struct spa_pod *>(spa_pod_builder_pop(b, &f[0]));
}

enum spa_video_format xdpd_format_pw_strip_alpha(enum spa_video_format format)
{
    switch (format) {
    case SPA_VIDEO_FORMAT_BGRA:
        return SPA_VIDEO_FORMAT_BGRx;
    case SPA_VIDEO_FORMAT_ABGR:
        return SPA_VIDEO_FORMAT_xBGR;
    case SPA_VIDEO_FORMAT_RGBA:
        return SPA_VIDEO_FORMAT_RGBx;
    case SPA_VIDEO_FORMAT_ARGB:
        return SPA_VIDEO_FORMAT_xRGB;
    case SPA_VIDEO_FORMAT_ARGB_210LE:
        return SPA_VIDEO_FORMAT_xRGB_210LE;
    case SPA_VIDEO_FORMAT_ABGR_210LE:
        return SPA_VIDEO_FORMAT_xBGR_210LE;
    case SPA_VIDEO_FORMAT_RGBA_102LE:
        return SPA_VIDEO_FORMAT_RGBx_102LE;
    case SPA_VIDEO_FORMAT_BGRA_102LE:
        return SPA_VIDEO_FORMAT_BGRx_102LE;
    default:
        return SPA_VIDEO_FORMAT_UNKNOWN;
    }
}

spa_pod *build_format(struct spa_pod_builder *b,
                      enum spa_video_format format,
                      uint32_t width,
                      uint32_t height,
                      uint32_t framerate,
                      uint64_t *modifiers,
                      int modifier_count)
{
    struct spa_pod_frame f[2];
    int i, c;

    enum spa_video_format format_without_alpha = xdpd_format_pw_strip_alpha(format);

    spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
    /* format */
    if (modifier_count > 0 || format_without_alpha == SPA_VIDEO_FORMAT_UNKNOWN) {
        // modifiers are defined only in combinations with their format
        // we should not announce the format without alpha
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
    } else {
        spa_pod_builder_add(b,
                            SPA_FORMAT_VIDEO_format,
                            SPA_POD_CHOICE_ENUM_Id(3, format, format, format_without_alpha),
                            0);
    }
    /* modifiers */
    if (modifier_count > 0) {
        // build an enumeration of modifiers
        spa_pod_builder_prop(b,
                             SPA_FORMAT_VIDEO_modifier,
                             SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE);
        spa_pod_builder_push_choice(b, &f[1], SPA_CHOICE_Enum, 0);
        // modifiers from the array
        for (i = 0, c = 0; i < modifier_count; i++) {
            spa_pod_builder_long(b, static_cast<int64_t>(modifiers[i]));
            if (c++ == 0)
                spa_pod_builder_long(b, static_cast<int64_t>(modifiers[i]));
        }
        spa_pod_builder_pop(b, &f[1]);
    }
    spa_pod_builder_add(b,
                        SPA_FORMAT_VIDEO_size,
                        SPA_POD_Rectangle(&SPA_RECTANGLE(width, height)),
                        0);
    // variable framerate
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&SPA_FRACTION(0, 1)), 0);
    spa_pod_builder_add(b,
                        SPA_FORMAT_VIDEO_maxFramerate,
                        SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION(framerate, 1),
                                                      &SPA_FRACTION(1, 1),
                                                      &SPA_FRACTION(framerate, 1)),
                        0);
    return static_cast<struct spa_pod *>(spa_pod_builder_pop(b, &f[0]));
}

enum spa_video_format xdpd_format_pw_from_drm_fourcc(uint32_t format)
{
    switch (format) {
    case DRM_FORMAT_ARGB8888:
        return SPA_VIDEO_FORMAT_BGRA;
    case DRM_FORMAT_XRGB8888:
        return SPA_VIDEO_FORMAT_BGRx;
    case DRM_FORMAT_RGBA8888:
        return SPA_VIDEO_FORMAT_ABGR;
    case DRM_FORMAT_RGBX8888:
        return SPA_VIDEO_FORMAT_xBGR;
    case DRM_FORMAT_ABGR8888:
        return SPA_VIDEO_FORMAT_RGBA;
    case DRM_FORMAT_XBGR8888:
        return SPA_VIDEO_FORMAT_RGBx;
    case DRM_FORMAT_BGRA8888:
        return SPA_VIDEO_FORMAT_ARGB;
    case DRM_FORMAT_BGRX8888:
        return SPA_VIDEO_FORMAT_xRGB;
    case DRM_FORMAT_NV12:
        return SPA_VIDEO_FORMAT_NV12;
    case DRM_FORMAT_XRGB2101010:
        return SPA_VIDEO_FORMAT_xRGB_210LE;
    case DRM_FORMAT_XBGR2101010:
        return SPA_VIDEO_FORMAT_xBGR_210LE;
    case DRM_FORMAT_RGBX1010102:
        return SPA_VIDEO_FORMAT_RGBx_102LE;
    case DRM_FORMAT_BGRX1010102:
        return SPA_VIDEO_FORMAT_BGRx_102LE;
    case DRM_FORMAT_ARGB2101010:
        return SPA_VIDEO_FORMAT_ARGB_210LE;
    case DRM_FORMAT_ABGR2101010:
        return SPA_VIDEO_FORMAT_ABGR_210LE;
    case DRM_FORMAT_RGBA1010102:
        return SPA_VIDEO_FORMAT_RGBA_102LE;
    case DRM_FORMAT_BGRA1010102:
        return SPA_VIDEO_FORMAT_BGRA_102LE;
    case DRM_FORMAT_BGR888:
        return SPA_VIDEO_FORMAT_RGB;
    case DRM_FORMAT_RGB888:
        return SPA_VIDEO_FORMAT_BGR;
    default:
        qCCritical(qLcScreenCastPipewire())
                << "Failed to convert drm format" << format << "to spa_video_format";
        return SPA_VIDEO_FORMAT_UNKNOWN;
    }
}

bool wlr_query_dmabuf_modifiers(ScreenCast *cast,
                                uint32_t drm_format,
                                uint32_t num_modifiers,
                                uint64_t *modifiers,
                                uint32_t *max_modifiers)
{
    if (cast->formatModifierPairs.isEmpty())
        return false;
    if (num_modifiers == 0) {
        *max_modifiers = 0;
        for (auto [fourcc, modifier] : cast->formatModifierPairs.asKeyValueRange()) {
            if (fourcc == drm_format
                && (modifier == DRM_FORMAT_MOD_INVALID
                    || gbm_device_get_format_modifier_plane_count(cast->ctx->gbm, fourcc, modifier)
                            > 0))
                *max_modifiers += 1;
        }
        return true;
    }

    uint32_t i = 0;
    for (auto [fourcc, modifier] : cast->formatModifierPairs.asKeyValueRange()) {
        if (i == num_modifiers)
            break;
        if (fourcc == drm_format
            && (modifier == DRM_FORMAT_MOD_INVALID
                || gbm_device_get_format_modifier_plane_count(cast->ctx->gbm, fourcc, modifier)
                        > 0)) {
            modifiers[i] = modifier;
            i++;
        }
    }
    *max_modifiers = num_modifiers;
    return true;
}

bool build_modifier_list(ScreenCast *cast,
                         uint32_t drm_format,
                         uint64_t **modifiers,
                         uint32_t *modifier_count)
{
    if (!wlr_query_dmabuf_modifiers(cast, drm_format, 0, nullptr, modifier_count)) {
        *modifiers = nullptr;
        *modifier_count = 0;
        return false;
    }
    if (*modifier_count == 0) {
        qCInfo(qLcScreenCastPipewire()) << "No modifiers available for format" << drm_format;
        *modifiers = nullptr;
        return true;
    }
    *modifiers = new uint64_t[*modifier_count];
    bool ret = wlr_query_dmabuf_modifiers(cast,
                                          drm_format,
                                          *modifier_count,
                                          *modifiers,
                                          modifier_count);
    qCInfo(qLcScreenCastPipewire()) << "num_modifiers" << *modifier_count;
    return ret;
}

uint32_t build_formats(struct spa_pod_builder *b[2],
                       ScreenCast *cast,
                       const struct spa_pod *params[2])
{
    uint32_t param_count;
    uint32_t modifier_count;
    uint64_t *modifiers = nullptr;

    if (!cast->avoidDmabufs
        && build_modifier_list(cast,
                               cast->screencopyFrameInfo[DMABUF].format,
                               &modifiers,
                               &modifier_count)
        && modifier_count > 0) {
        param_count = 2;
        params[0] = build_format(
                b[0],
                xdpd_format_pw_from_drm_fourcc(cast->screencopyFrameInfo[DMABUF].format),
                cast->screencopyFrameInfo[DMABUF].width,
                cast->screencopyFrameInfo[DMABUF].height,
                cast->framerate,
                modifiers,
                static_cast<int>(modifier_count));
        Q_ASSERT(params[0] != nullptr);
        params[1] = build_format(
                b[1],
                xdpd_format_pw_from_drm_fourcc(cast->screencopyFrameInfo[WL_SHM].format),
                cast->screencopyFrameInfo[WL_SHM].width,
                cast->screencopyFrameInfo[WL_SHM].height,
                cast->framerate,
                nullptr,
                0);
        Q_ASSERT(params[1] != nullptr);
    } else {
        param_count = 1;
        params[0] = build_format(
                b[0],
                xdpd_format_pw_from_drm_fourcc(cast->screencopyFrameInfo[WL_SHM].format),
                cast->screencopyFrameInfo[WL_SHM].width,
                cast->screencopyFrameInfo[WL_SHM].height,
                cast->framerate,
                nullptr,
                0);
        Q_ASSERT(params[0] != nullptr);
    }
    free(modifiers);
    return param_count;
}

void pwr_update_stream_param(ScreenCast *cast)
{
    qCDebug(qLcScreenCastPipewire()) << "stream update parameters";
    struct pw_stream *stream = cast->stream;
    uint8_t params_buffer[2][1024];
    struct spa_pod_dynamic_builder b[2];
    spa_pod_dynamic_builder_init(&b[0], params_buffer[0], sizeof(params_buffer[0]), 2048);
    spa_pod_dynamic_builder_init(&b[1], params_buffer[1], sizeof(params_buffer[1]), 2048);
    const struct spa_pod *params[2];

    struct spa_pod_builder *builder[2] = { &b[0].b, &b[1].b };
    uint32_t n_params = build_formats(builder, cast, params);

    pw_stream_update_params(stream, params, n_params);
    spa_pod_dynamic_builder_clean(&b[0]);
    spa_pod_dynamic_builder_clean(&b[1]);
}

void xdpd_pwr_enqueue_buffer(ScreenCast *cast)
{
    qCDebug(qLcScreenCastPipewire) << "enqueuing buffer";

    if (!cast->currentFrame.pwBuffer) {
        qCWarning(qLcScreenCastPipewire) << "no buffer to queue";
        cast->currentFrame.xdpdBuffer = nullptr;
        cast->currentFrame.pwBuffer = nullptr;
        return;
    }
    struct pw_buffer *pwBuf = cast->currentFrame.pwBuffer;
    struct spa_buffer *spaBuf = pwBuf->buffer;
    struct spa_data *d = spaBuf->datas;

    bool bufferCorrupt = cast->frameState != XDPD_FRAME_STATE_SUCCESS;

    if (cast->currentFrame.yInverted) {
        // TODO: Flip buffer or set stride negative
        bufferCorrupt = true;
        cast->err = 1;
    }

    qCDebug(qLcScreenCastPipewire) << "********************";
    struct spa_meta_header *h;
    if ((h = static_cast<spa_meta_header *>(
                 spa_buffer_find_meta_data(spaBuf, SPA_META_Header, sizeof(*h))))) {
        h->pts = SPA_TIMESPEC_TO_NSEC(&cast->currentFrame);
        h->flags = bufferCorrupt ? SPA_META_HEADER_FLAG_CORRUPTED : 0;
        h->seq = cast->seq++;
        h->dts_offset = 0;
        qCDebug(qLcScreenCastPipewire) << "timestamp" << h->pts;
    }

    struct spa_meta_videotransform *vt;
    if ((vt = static_cast<struct spa_meta_videotransform *>(
                 spa_buffer_find_meta_data(spaBuf, SPA_META_VideoTransform, sizeof(*vt))))) {
        vt->transform = cast->currentFrame.transformation;
        qCDebug(qLcScreenCastPipewire) << "transformation" << vt->transform;
    }

    struct spa_meta *damage;
    if ((damage = spa_buffer_find_meta(spaBuf, SPA_META_VideoDamage))) {
        auto *dRegion = static_cast<struct spa_region *>(spa_meta_first(damage));
        uint32_t damageCounter = 0;
        do {
            if (damageCounter >= cast->currentFrame.damageCount) {
                *dRegion = SPA_REGION(0, 0, 0, 0);
                qCDebug(qLcScreenCastPipewire)
                        << "end damage" << damageCounter << dRegion->position.x
                        << dRegion->position.y << dRegion->size.width << dRegion->size.height;
            }
            struct XdpdFrameDamage *fdamage = &cast->currentFrame.damages[damageCounter];
            *dRegion = SPA_REGION(fdamage->x, fdamage->y, fdamage->width, fdamage->height);
            qCDebug(qLcScreenCastPipewire)
                    << "damage" << damageCounter << dRegion->position.x << dRegion->position.y
                    << dRegion->size.width << dRegion->size.height;
        } while (spa_meta_check(dRegion + 1, damage) && dRegion++);

        if (damageCounter < cast->currentFrame.damageCount) {
            struct XdpdFrameDamage fdamage = { dRegion->position.x,
                                               dRegion->position.y,
                                               dRegion->size.width,
                                               dRegion->size.height };
            for (; damageCounter < cast->currentFrame.damageCount; damageCounter++) {
                fdamage = mergeDamage(&fdamage, &cast->currentFrame.damages[damageCounter]);
            }
            *dRegion = SPA_REGION(fdamage.x, fdamage.y, fdamage.width, fdamage.height);
            qCDebug(qLcScreenCastPipewire)
                    << "collected damage" << damageCounter << dRegion->position.x
                    << dRegion->position.y << dRegion->size.width << dRegion->size.height;
        }
    }

    if (bufferCorrupt) {
        for (uint32_t plane = 0; plane < spaBuf->n_datas; plane++) {
            d[plane].chunk->flags = SPA_CHUNK_FLAG_CORRUPTED;
        }
    } else {
        for (uint32_t plane = 0; plane < spaBuf->n_datas; plane++) {
            d[plane].chunk->flags = SPA_CHUNK_FLAG_NONE;
        }
    }

    for (uint32_t plane = 0; plane < spaBuf->n_datas; plane++) {
        qCDebug(qLcScreenCastPipewire) << "plane" << plane;
        qCDebug(qLcScreenCastPipewire) << "fd" << d[plane].fd;
        qCDebug(qLcScreenCastPipewire) << "maxsize" << d[plane].maxsize;
        qCDebug(qLcScreenCastPipewire) << "size" << d[plane].chunk->size;
        qCDebug(qLcScreenCastPipewire) << "stride" << d[plane].chunk->stride;
        qCDebug(qLcScreenCastPipewire) << "offset" << d[plane].chunk->offset;
        qCDebug(qLcScreenCastPipewire) << "chunk flags" << d[plane].chunk->flags;
    }
    qCDebug(qLcScreenCastPipewire) << "width" << cast->currentFrame.xdpdBuffer->width;
    qCDebug(qLcScreenCastPipewire) << "height" << cast->currentFrame.xdpdBuffer->height;
    qCDebug(qLcScreenCastPipewire) << "yInvert" << cast->currentFrame.yInverted;
    qCDebug(qLcScreenCastPipewire) << "********************";

    pw_stream_queue_buffer(cast->stream, pwBuf);

    cast->currentFrame.xdpdBuffer = nullptr;
    cast->currentFrame.pwBuffer = nullptr;
}

void pwr_handle_stream_state_changed(void *data,
                                     enum pw_stream_state old,
                                     enum pw_stream_state state,
                                     [[maybe_unused]] const char *error)
{
    auto cast = static_cast<ScreenCast *>(data);
    cast->nodeId = pw_stream_get_node_id(cast->stream);

    qCDebug(qLcScreenCastPipewire)
            << "stream state changed to" << QString::fromUtf8(pw_stream_state_as_string(state));
    qCDebug(qLcScreenCastPipewire) << "node id is" << cast->nodeId;

    switch (state) {
    case PW_STREAM_STATE_STREAMING:
        cast->pwrStreamState = true;
        break;
    case PW_STREAM_STATE_PAUSED:
        if (old == PW_STREAM_STATE_STREAMING) {
            xdpd_pwr_enqueue_buffer(cast);
        }
        [[fallthrough]];
    default:
        cast->pwrStreamState = false;
        break;
    }
}

struct spa_pod *fixate_format(struct spa_pod_builder *b,
                              enum spa_video_format format,
                              uint32_t width,
                              uint32_t height,
                              uint32_t framerate,
                              const uint64_t *modifier)
{
    struct spa_pod_frame f[1];

    enum spa_video_format format_without_alpha = xdpd_format_pw_strip_alpha(format);

    spa_pod_builder_push_object(b, &f[0], SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(b, SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video), 0);
    spa_pod_builder_add(b, SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), 0);
    /* format */
    if (modifier || format_without_alpha == SPA_VIDEO_FORMAT_UNKNOWN) {
        spa_pod_builder_add(b, SPA_FORMAT_VIDEO_format, SPA_POD_Id(format), 0);
    } else {
        spa_pod_builder_add(b,
                            SPA_FORMAT_VIDEO_format,
                            SPA_POD_CHOICE_ENUM_Id(3, format, format, format_without_alpha),
                            0);
    }
    /* modifiers */
    if (modifier) {
        // implicit modifier
        spa_pod_builder_prop(b, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
        spa_pod_builder_long(b, static_cast<int64_t>(*modifier));
    }
    spa_pod_builder_add(b,
                        SPA_FORMAT_VIDEO_size,
                        SPA_POD_Rectangle(&SPA_RECTANGLE(width, height)),
                        0);
    // variable framerate
    spa_pod_builder_add(b, SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(&SPA_FRACTION(0, 1)), 0);
    spa_pod_builder_add(b,
                        SPA_FORMAT_VIDEO_maxFramerate,
                        SPA_POD_CHOICE_RANGE_Fraction(&SPA_FRACTION(framerate, 1),
                                                      &SPA_FRACTION(1, 1),
                                                      &SPA_FRACTION(framerate, 1)),
                        0);
    return static_cast<spa_pod *>(spa_pod_builder_pop(b, &f[0]));
}

void pwr_handle_stream_param_changed(void *data, uint32_t id, const struct spa_pod *param)
{
    qCDebug(qLcScreenCastPipewire) << "stream parameters changed";
    auto cast = static_cast<ScreenCast *>(data);
    auto stream = cast->stream;
    uint8_t params_buffer[3][1024];
    struct spa_pod_dynamic_builder b[3];
    const struct spa_pod *params[4];
    uint32_t blocks;
    uint32_t data_type;

    if (!param || id != SPA_PARAM_Format) {
        return;
    }

    spa_pod_dynamic_builder_init(&b[0], params_buffer[0], sizeof(params_buffer[0]), 2048);
    spa_pod_dynamic_builder_init(&b[1], params_buffer[1], sizeof(params_buffer[1]), 2048);
    spa_pod_dynamic_builder_init(&b[2], params_buffer[2], sizeof(params_buffer[2]), 2048);

    spa_format_video_raw_parse(param, &cast->pwrFormat);
    cast->framerate =
            (uint32_t)(cast->pwrFormat.max_framerate.num / cast->pwrFormat.max_framerate.denom);

    const struct spa_pod_prop *prop_modifier;
    if ((prop_modifier = spa_pod_find_prop(param, nullptr, SPA_FORMAT_VIDEO_modifier)) != nullptr) {
        cast->bufferType = DMABUF;
        data_type = 1 << SPA_DATA_DmaBuf;
        Q_ASSERT(cast->pwrFormat.format
                 == xdpd_format_pw_from_drm_fourcc(cast->screencopyFrameInfo[DMABUF].format));
        if ((prop_modifier->flags & SPA_POD_PROP_FLAG_DONT_FIXATE) > 0) {
            const struct spa_pod *pod_modifier = &prop_modifier->value;

            uint32_t n_modifiers = SPA_POD_CHOICE_N_VALUES(pod_modifier) - 1;
            auto modifiers = static_cast<uint64_t *>(SPA_POD_CHOICE_VALUES(pod_modifier));
            modifiers++;
            uint32_t flags = GBM_BO_USE_RENDERING;
            uint64_t modifier;
            uint32_t n_params;
            struct spa_pod_builder *builder[2] = { &b[0].b, &b[1].b };

            struct gbm_bo *bo = gbm_bo_create_with_modifiers2(
                    cast->ctx->gbm,
                    cast->screencopyFrameInfo[cast->bufferType].width,
                    cast->screencopyFrameInfo[cast->bufferType].height,
                    cast->screencopyFrameInfo[cast->bufferType].format,
                    modifiers,
                    n_modifiers,
                    flags);
            if (bo) {
                modifier = gbm_bo_get_modifier(bo);
                gbm_bo_destroy(bo);
                goto fixate_format;
            }

            qCDebug(qLcScreenCastPipewire)
                    << "unable to allocate a dmabuf with modifiers. Falling back to the old api";
            for (uint32_t i = 0; i < n_modifiers; i++) {
                switch (modifiers[i]) {
                case DRM_FORMAT_MOD_INVALID:
                    flags = cast->ctx->state->config.forceModLinear
                            ? GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR
                            : GBM_BO_USE_RENDERING;
                    break;
                case DRM_FORMAT_MOD_LINEAR:
                    flags = GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR;
                    break;
                default:
                    continue;
                }
                bo = gbm_bo_create(cast->ctx->gbm,
                                   cast->screencopyFrameInfo[cast->bufferType].width,
                                   cast->screencopyFrameInfo[cast->bufferType].height,
                                   cast->screencopyFrameInfo[cast->bufferType].format,
                                   flags);
                if (bo) {
                    modifier = gbm_bo_get_modifier(bo);
                    gbm_bo_destroy(bo);
                    goto fixate_format;
                }
            }

            qCDebug(qLcScreenCastPipewire) << "unable to allocate a dmabuf. Falling back to shm";
            cast->avoidDmabufs = true;

            n_params = build_formats(builder, cast, &params[0]);
            pw_stream_update_params(stream, params, n_params);
            spa_pod_dynamic_builder_clean(&b[0]);
            spa_pod_dynamic_builder_clean(&b[1]);
            spa_pod_dynamic_builder_clean(&b[2]);
            return;

        fixate_format:
            params[0] = fixate_format(&b[2].b,
                                      xdpd_format_pw_from_drm_fourcc(
                                              cast->screencopyFrameInfo[cast->bufferType].format),
                                      cast->screencopyFrameInfo[cast->bufferType].width,
                                      cast->screencopyFrameInfo[cast->bufferType].height,
                                      cast->framerate,
                                      &modifier);

            n_params = build_formats(builder, cast, &params[1]);
            n_params++;

            pw_stream_update_params(stream, params, n_params);
            spa_pod_dynamic_builder_clean(&b[0]);
            spa_pod_dynamic_builder_clean(&b[1]);
            spa_pod_dynamic_builder_clean(&b[2]);
            return;
        }

        if (cast->pwrFormat.modifier == DRM_FORMAT_MOD_INVALID) {
            blocks = 1;
        } else {
            blocks = gbm_device_get_format_modifier_plane_count(
                    cast->ctx->gbm,
                    cast->screencopyFrameInfo[DMABUF].format,
                    cast->pwrFormat.modifier);
        }
    } else {
        cast->bufferType = WL_SHM;
        blocks = 1;
        data_type = 1 << SPA_DATA_MemFd;
    }

    qCDebug(qLcScreenCastPipewire) << "Format negotiated:";
    qCDebug(qLcScreenCastPipewire) << "buffer_type:" << cast->bufferType << "(" << data_type << ")";
    qCDebug(qLcScreenCastPipewire) << "format:" << cast->pwrFormat.format;
    qCDebug(qLcScreenCastPipewire) << "modifier:" << cast->pwrFormat.modifier;
    qCDebug(qLcScreenCastPipewire) << "size: (" << cast->pwrFormat.size.width << ", "
                                   << cast->pwrFormat.size.height << ")";
    qCDebug(qLcScreenCastPipewire) << "max_framerate: (" << cast->pwrFormat.max_framerate.num
                                   << " / " << cast->pwrFormat.max_framerate.denom << ")";

    params[0] = build_buffer(&b[0].b,
                             blocks,
                             cast->screencopyFrameInfo[cast->bufferType].size,
                             cast->screencopyFrameInfo[cast->bufferType].stride,
                             data_type);

    params[1] = static_cast<struct spa_pod *>(
            spa_pod_builder_add_object(&b[1].b,
                                       SPA_TYPE_OBJECT_ParamMeta,
                                       SPA_PARAM_Meta,
                                       SPA_PARAM_META_type,
                                       SPA_POD_Id(SPA_META_Header),
                                       SPA_PARAM_META_size,
                                       SPA_POD_Int(sizeof(struct spa_meta_header))));

    params[2] = static_cast<struct spa_pod *>(
            spa_pod_builder_add_object(&b[1].b,
                                       SPA_TYPE_OBJECT_ParamMeta,
                                       SPA_PARAM_Meta,
                                       SPA_PARAM_META_type,
                                       SPA_POD_Id(SPA_META_VideoTransform),
                                       SPA_PARAM_META_size,
                                       SPA_POD_Int(sizeof(struct spa_meta_videotransform))));

    params[3] = static_cast<struct spa_pod *>(spa_pod_builder_add_object(
            &b[2].b,
            SPA_TYPE_OBJECT_ParamMeta,
            SPA_PARAM_Meta,
            SPA_PARAM_META_type,
            SPA_POD_Id(SPA_META_VideoDamage),
            SPA_PARAM_META_size,
            SPA_POD_CHOICE_RANGE_Int(sizeof(struct spa_meta_region) * 4,
                                     sizeof(struct spa_meta_region) * 1,
                                     sizeof(struct spa_meta_region) * 4)));

    pw_stream_update_params(stream, params, 4);
    spa_pod_dynamic_builder_clean(&b[0]);
    spa_pod_dynamic_builder_clean(&b[1]);
    spa_pod_dynamic_builder_clean(&b[2]);
}

void pwr_handle_stream_add_buffer(void *data, struct pw_buffer *buffer)
{
    auto *cast = static_cast<ScreenCast *>(data);
    struct spa_data *d;
    enum spa_data_type t;

    qCDebug(qLcScreenCastPipewire) << "add buffer event handle";

    d = buffer->buffer->datas;

    // Select buffer type from negotiation result
    if ((d[0].type & (1u << SPA_DATA_MemFd)) > 0) {
        Q_ASSERT(cast->bufferType == WL_SHM);
        t = SPA_DATA_MemFd;
    } else if ((d[0].type & (1u << SPA_DATA_DmaBuf)) > 0) {
        Q_ASSERT(cast->bufferType == DMABUF);
        t = SPA_DATA_DmaBuf;
    } else {
        qCCritical(qLcScreenCastPipewire) << "unsupported buffer type";
        cast->err = 1;
        return;
    }

    qCDebug(qLcScreenCastPipewire) << "selected buffer type" << t;

    struct XdpdBuffer *xdpdBuffer =
            xdpdBufferCreate(cast, cast->bufferType, &cast->screencopyFrameInfo[cast->bufferType]);
    if (xdpdBuffer == nullptr) {
        qCCritical(qLcScreenCastPipewire) << "failed to create xdpw buffer";
        cast->err = 1;
        return;
    }
    cast->bufferList << xdpdBuffer;
    buffer->user_data = xdpdBuffer;

    Q_ASSERT(xdpdBuffer->plane_count >= 0
             && buffer->buffer->n_datas == static_cast<uint32_t>(xdpdBuffer->plane_count));
    for (uint32_t plane = 0; plane < buffer->buffer->n_datas; plane++) {
        d[plane].type = t;
        d[plane].maxsize = xdpdBuffer->size[plane];
        d[plane].mapoffset = 0;
        d[plane].chunk->size = xdpdBuffer->size[plane];
        d[plane].chunk->stride = static_cast<int32_t>(xdpdBuffer->stride[plane]);
        d[plane].chunk->offset = xdpdBuffer->offset[plane];
        d[plane].flags = 0;
        d[plane].fd = xdpdBuffer->fd[plane];
        d[plane].data = nullptr;
        // clients have implemented to check chunk->size if the buffer is valid instead
        // of using the flags. Until they are patched we should use some arbitrary value.
        if (xdpdBuffer->buffer_type == DMABUF && d[plane].chunk->size == 0) {
            d[plane].chunk->size = 9; // This was chosen by a fair d20.
        }
    }
}

void pwr_handle_stream_remove_buffer(void *data, struct pw_buffer *buffer)
{
    auto *cast = static_cast<ScreenCast *>(data);

    qCDebug(qLcScreenCastPipewire) << "remove buffer event handle";

    auto *xdpwBuffer = static_cast<XdpdBuffer *>(buffer->user_data);
    if (xdpwBuffer) {
        xdpdBufferDestroy(xdpwBuffer);
    }
    if (cast->currentFrame.pwBuffer == buffer) {
        cast->currentFrame.pwBuffer = nullptr;
    }
    for (uint32_t plane = 0; plane < buffer->buffer->n_datas; plane++) {
        buffer->buffer->datas[plane].fd = -1;
    }
    buffer->user_data = nullptr;
}

void xdpd_pwr_dequeue_buffer(ScreenCast *cast)
{
    qCDebug(qLcScreenCastPipewire) << "dequeue buffer";

    Q_ASSERT(!cast->currentFrame.pwBuffer);
    if (!(cast->currentFrame.pwBuffer = pw_stream_dequeue_buffer(cast->stream))) {
        qCWarning(qLcScreenCastPipewire) << "out of buffers";
        return;
    }

    cast->currentFrame.xdpdBuffer =
            static_cast<XdpdBuffer *>(cast->currentFrame.pwBuffer->user_data);
}

void xdpd_wlr_sc_frame_capture(ScreenCast *cast)
{
    qCDebug(qLcScreenCastPipewire) << "start screencopy";
    if (cast->quit || cast->err) {
        // TODO: delete cast instance
        // xdpw_screencast_instance_destroy(cast);
        return;
    }

    if (cast->initialized && !cast->pwrStreamState) {
        cast->frameState = XDPD_FRAME_STATE_NONE;
        return;
    }

    cast->frameState = XDPD_FRAME_STATE_STARTED;
    cast->castFrame =
            cast->ctx->screencopyManager->captureOutput<ScreenCastFrame>(0,
                                                                         cast->target.output,
                                                                         cast,
                                                                         nullptr);
}

void xdpd_wlr_frame_capture(ScreenCast *cast)
{
    if (cast->ctx->screencopyManager) {
        xdpd_wlr_sc_frame_capture(cast);
    }
}

void pwr_handle_stream_on_process(void *data)
{
    auto cast = static_cast<ScreenCast *>(data);

    qCDebug(qLcScreenCastPipewire) << "on process event handle";

    if (!cast->pwrStreamState) {
        qCDebug(qLcScreenCastPipewire) << "not streaming";
        return;
    }

    if (cast->currentFrame.pwBuffer) {
        qCDebug(qLcScreenCastPipewire) << "buffer already exported";
        return;
    }

    xdpd_pwr_dequeue_buffer(cast);
    if (!cast->currentFrame.pwBuffer) {
        qCWarning(qLcScreenCastPipewire) << "unable to export buffer";
        return;
    }
    if (cast->seq > 0) {
        quint64 delay_ns = fps_limit_measure_end(&cast->fps_limit, cast->framerate);
        if (delay_ns > 0) {
            QTimer::singleShot(delay_ns, [cast]() {
                xdpd_wlr_frame_capture(cast);
            });
            return;
        }
    }
    xdpd_wlr_frame_capture(cast);
}