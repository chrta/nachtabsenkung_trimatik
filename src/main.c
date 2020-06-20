/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>

#include <sys/printk.h>

#include "controller.h"

void main(void)
{
	void* controller = ctrl_init();
	
	if (!controller) {
		printk("Failed to init ctrl\n");
		return;
	}

	while (1) {
		k_msleep(10000);
	}
}
