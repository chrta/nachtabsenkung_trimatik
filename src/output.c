/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "output.h"

#include <sys/printk.h>
#include <drivers/gpio.h>

#if defined(CONFIG_BOARD_NUCLEO_F446RE)
#define GPIO_PIN_OFF		0	/* PC0 */
#define GPIO_PORT_OFF	        "GPIOC"
#define GPIO_PIN_NIGHT		3	/* PC3 */
#define GPIO_PORT_NIGHT	        "GPIOC"
#else
#error "unsupported board"
#endif

#define HIGH				1
#define LOW				0

struct gpio_info {
	const char* port;
	const uint8_t index;
	struct device *dev;
};

/* day is no outut active */
enum gpio_index {
	OUTPUT_IDX_OFF = 0,
	OUTPUT_IDX_NIGHT
};

static struct gpio_info global_gpios[] = {
	{GPIO_PORT_OFF, GPIO_PIN_OFF, NULL},
	{GPIO_PORT_NIGHT, GPIO_PIN_NIGHT, NULL}
};

static bool gpio_init(struct gpio_info* gpios, int length)
{
	for (int i = 0; i < length; i++) {
		gpios[i].dev = device_get_binding(gpios[i].port);
		if (!gpios[i].dev) {
			printk("Cannot find %s!\n", gpios[i].port);
			return false;
		}
		if (gpio_pin_configure(gpios[i].dev, gpios[i].index, GPIO_OUTPUT)) {
			printk("Failed to set %s_%d as output\n", gpios[i].port, gpios[i].index);
			return false;
		}
	}
	return true;
}

static inline void output_gpio_write(struct gpio_info* gpios, enum gpio_index idx, int value)
{
	struct gpio_info *gi = &gpios[idx];
	if (gpio_pin_set_raw(gi->dev, gi->index, value)) {
		printk("Failed to set idx %d to %d\n", idx, value);
	}
}

void output_set(void *dev, enum output_type type)
{
	struct gpio_info* gpios = dev;
	printk("Set output pins for %d\n", type);
	
	switch (type) {
	case OUTPUT_DAY:
		output_gpio_write(gpios, OUTPUT_IDX_OFF, LOW);
		output_gpio_write(gpios, OUTPUT_IDX_NIGHT, LOW);
		break;
	case OUTPUT_NIGHT:
		output_gpio_write(gpios, OUTPUT_IDX_OFF, LOW);
		output_gpio_write(gpios, OUTPUT_IDX_NIGHT, HIGH);
		break;
	case OUTPUT_OFF:
	default:
		output_gpio_write(gpios, OUTPUT_IDX_NIGHT, LOW);
		output_gpio_write(gpios, OUTPUT_IDX_OFF, HIGH);
		break;		
	}
}

void *output_init(void)
{
	struct gpio_info* gpios = global_gpios;
	int ret;

	ret = gpio_init(gpios, ARRAY_SIZE(global_gpios));
	
	if (!ret) {
		printk("Failed to init output gpios\n");
		return NULL;
	}

	return gpios;
}
