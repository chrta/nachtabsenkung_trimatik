/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_BUTTONS_H
#define APP_BUTTONS_H

#include <zephyr.h>

enum button_type {
	BUTTON_NONE,
	BUTTON_SELECT,
	BUTTON_LEFT,
	BUTTON_RIGHT,
	BUTTON_UP,
	BUTTON_DOWN,
};

#define BUTTON_RELEASED false
#define BUTTON_PRESSED true

typedef void (button_cb)(void* dev, enum button_type, bool);

void *buttons_init(button_cb cb);

bool buttons_poll(void *dev, enum button_type *type);

#endif /*APP_BUTTONS_H*/
