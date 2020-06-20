/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "buttons.h"

#include <zephyr.h>

#include <sys/printk.h>
#include <drivers/adc.h>

#include <string.h>

#if defined(CONFIG_BOARD_NUCLEO_F429ZI)
#define ADC_DEVICE_NAME         DT_LABEL(DT_INST(0, st_stm32_adc))
#define ADC_RESOLUTION		12
#define ADC_GAIN		ADC_GAIN_1
#define ADC_REFERENCE		ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID	3
#elif defined(CONFIG_BOARD_NUCLEO_F446RE)
#define ADC_DEVICE_NAME         DT_LABEL(DT_INST(0, st_stm32_adc))
#define ADC_RESOLUTION		12
#define ADC_GAIN		ADC_GAIN_1
#define ADC_REFERENCE		ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID	0
#else
#error "Unsupported board"
#endif

static const struct adc_channel_cfg adc_channel_cfg = {
	.gain             = ADC_GAIN,
	.reference        = ADC_REFERENCE,
	.acquisition_time = ADC_ACQUISITION_TIME,
	.channel_id       = ADC_CHANNEL_ID,
};

#define BUFFER_SIZE  6
static int16_t m_sample_buffer[BUFFER_SIZE];

enum adc_action adc_handler(struct device *dev,
			    const struct adc_sequence *sequence,
			    uint16_t sampling_index)
{
	int16_t v = m_sample_buffer[0];
	printk("Sample %d\n", v);
	return ADC_ACTION_REPEAT;
}

struct adc_sequence_options options = {
	.interval_us = 1000000, /* once a second */
	.callback = adc_handler
};

static const struct adc_sequence sequence = {
	.options     = NULL /*&options*/,
	.channels    = BIT(ADC_CHANNEL_ID),
	.buffer      = m_sample_buffer,
	.buffer_size = sizeof(m_sample_buffer),
	.resolution  = ADC_RESOLUTION,
};


#define BUTTON_STACK_SIZE 500
#define BUTTON_THREAD_PRIORITY 5

#define ADC_DELTA 10
#define ADC_BUTTON_CHANGE 100

static int diff(int v1, int v2)
{
	if (v1 > v2) {
		return v1 - v2;
	}

	return v2 - v1;
}

static bool button_decode(int16_t v, enum button_type* type)
{
	if (v > 4080) {
		*type = BUTTON_SELECT;
		return true;
	}
	
	if (v > 2900) {
		*type = BUTTON_NONE;
		return true;
	}

	if (v > 2400) {
		*type = BUTTON_LEFT;
		return true;
	}

	if (v > 1700) {
		*type = BUTTON_DOWN;
		return true;
	}

	if (v > 750) {
		*type = BUTTON_UP;
		return true;
	}


	if (v < 100) {
		*type = BUTTON_RIGHT;
		return true;
	}

	return false;
}

static void button_func(void *adc_dev, void *cb_func, void *u3)
{
	int ret;
	int prev_v = 0;
	int prev_stable = 0;
	bool res;
	enum button_type type;
	enum button_type prev_type = BUTTON_NONE;
	struct device *dev = adc_dev;
	button_cb *cb = cb_func;
	
	for(;;) {
		
		ret = adc_read(dev, &sequence);
		
		if (ret) {
			printk("Failed to read from adc\n");
			return;
		}
		
		int16_t v = m_sample_buffer[0];
		if (diff(v, prev_v) < ADC_DELTA) {
			/* measurement is ok, use it */
			if (diff(v, prev_stable) > ADC_BUTTON_CHANGE) {
				printk("ADC change from %d to %d\n", prev_stable, v);
				res = button_decode(v, &type);
				if (prev_type != type) {
					if (type == BUTTON_NONE) {
						cb(dev, prev_type, BUTTON_RELEASED);
					} else {
						cb(dev, type, BUTTON_PRESSED);						
					}
					prev_type = type;
				}
				prev_stable = v;
			}
		}
		prev_v = v;
		k_msleep(50);
	}
}

K_THREAD_STACK_DEFINE(button_stack_area, BUTTON_STACK_SIZE);
static struct k_thread button_thread_data;

void *buttons_init(button_cb cb)
{
	int ret;
	struct device *adc_dev = device_get_binding(ADC_DEVICE_NAME);

	if (!adc_dev) {
		printk("Failed to get adc dev\n");
		return NULL;
	}

	ret = adc_channel_setup(adc_dev, &adc_channel_cfg);

	if (ret) {
		printk("Failed to init adc\n");
		return NULL;
	}

	(void)memset(m_sample_buffer, 0, sizeof(m_sample_buffer));

	k_thread_create(&button_thread_data, button_stack_area,
			K_THREAD_STACK_SIZEOF(button_stack_area),
			button_func,
			adc_dev, cb, NULL,
			BUTTON_THREAD_PRIORITY, 0, K_NO_WAIT);

	return adc_dev;
}

bool buttons_poll(void *btn_dev, enum button_type *type)
{
	struct device* dev = btn_dev; 
	int ret;
	
	ret = adc_read(dev, &sequence);

	if (ret) {
		printk("Failed to read from adc\n");
		return false;
	}

	int16_t v = m_sample_buffer[0];

	printk("Sample value %d\n", v);
	if (button_decode(v, type)) {
		return true;
	}

	printk("Cannot detect button for %d\n", v);
	return false;
}
