// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <ctime>
#include <cstdint>

#define TIMESPEC_NSEC_PER_SEC 1000000000L

void timespec_add(struct timespec *t, int64_t delta_ns);

bool timespec_less(struct timespec *t1, struct timespec *t2);

bool timespec_is_zero(struct timespec *t);

int64_t timespec_diff_ns(struct timespec *t1, struct timespec *t2);
