// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <ctime>

struct fps_limit_state
{
    struct timespec frame_last_time;
    struct timespec fps_last_time;
    uint64_t fps_frame_count;
};

void fps_limit_measure_start(struct fps_limit_state *state, double max_fps);

uint64_t fps_limit_measure_end(struct fps_limit_state *state, double max_fps);

