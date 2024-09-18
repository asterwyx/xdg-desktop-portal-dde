// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "qwayland-linux-dmabuf-unstable-v1.h"

#include <wayland-client-protocol.h>

#include <QObject>

class LinuxDmabuf : public QObject, public QtWayland::zwp_linux_dmabuf_v1
{
    Q_OBJECT
public:
    explicit LinuxDmabuf(struct ::zwp_linux_dmabuf_v1 *object)
        : QtWayland::zwp_linux_dmabuf_v1(object)
    {
    }
};
