/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include <time.h>
#include <stdbool.h>

void* clock_init(void);

struct tm* clock_rtc_read(void* dev);

void clock_rtc_set(void *dev, const struct tm *now);

bool clock_rtc_reg_read(void* buffer, size_t len);

bool clock_rtc_reg_write(const void* buffer, size_t len);

#endif /* APP_CLOCK_H */
