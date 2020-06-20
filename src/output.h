/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_OUTPUT_H
#define APP_OUTPUT_H

#include <zephyr.h>

enum output_type {
	OUTPUT_DAY,
	OUTPUT_NIGHT,
	OUTPUT_OFF,
};

void *output_init(void);

void output_set(void *dev, enum output_type type);

#endif /*APP_OUTPUT_H*/
