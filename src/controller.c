/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(ctr, CONFIG_LOG_DEFAULT_LEVEL);

#include "controller.h"

#include "lcd.h"
#include "buttons.h"
#include "clock.h"
#include "output.h"

#include <string.h>
#include <stdio.h>


#define CTRL_STACK_SIZE 2000
#define CTRL_THREAD_PRIORITY 6

static const char* DAY_STR[] =
{"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

static const char* MODE_STR[] =
{"Aus", "Tag", "Nacht"};

enum op_mode {
	OP_MODE_OFF = 0,
	OP_MODE_DAY,
	OP_MODE_NIGHT
};

struct ctrl_time {
	uint8_t hour;
	uint8_t minute;
};

struct ctrl_settings {
	struct ctrl_time day_begin;
	struct ctrl_time day_end;
};

#define PERSISTENT_SETTINGS_MAGIC (0xAA551234)

struct persistent_ctrl_settings {
	uint32_t magic_no;
	struct ctrl_settings settings;
} __attribute__((packed));

struct cursor {
	uint8_t row;
	uint8_t col;
	bool blinking;
};

enum input_mode {
	INPUT_MODE_VIEW = 0,
	INPUT_MODE_EDIT_CLOCK_HOUR,
	INPUT_MODE_EDIT_CLOCK_MINUTE,
	INPUT_MODE_EDIT_DAY,
	INPUT_MODE_EDIT_SCHEDULE_BEGIN_HOUR,
	INPUT_MODE_EDIT_SCHEDULE_BEGIN_MINUTE,
	INPUT_MODE_EDIT_SCHEDULE_END_HOUR,
	INPUT_MODE_EDIT_SCHEDULE_END_MINUTE,
	INPUT_MODE_LAST //must be always last entry
};

struct ctx {
	void* lcd;
	void* buttons;
	void* output;
	void* clock;

	struct cursor cursor;

	enum op_mode mode;
	struct ctrl_settings settings;

	enum input_mode input_mode;
};

static struct ctx ctrl_ctx;

struct msgq_item_t {
	uint8_t button_index;
	uint8_t button_pressed;
	uint32_t duration_msec;
};

K_MSGQ_DEFINE(ctrl_msgq, sizeof(struct msgq_item_t), 30, 4);

K_THREAD_STACK_DEFINE(ctrl_stack_area, CTRL_STACK_SIZE);
static struct k_thread ctrl_thread_data;

static void user_input_expiry_function(struct k_timer *timer_id);

K_TIMER_DEFINE(user_input_timer, user_input_expiry_function, NULL);

#if 0
void show_date_time(void *lcd, struct tm *now)
{
	static char time_buffer[16];
	static char date_buffer[16];

	sprintf(date_buffer, "%02d.%02d.%02d", now->tm_mday,
		now->tm_mon + 1, now->tm_year - 100);
	sprintf(time_buffer, "%02d:%02d:%02d", now->tm_hour, now->tm_min, now->tm_sec);
	//lcd_clear(lcd);
	/*k_msleep(MSEC_PER_SEC * 3U);*/
	lcd_set_cursor(lcd, 0, 0);
	
	lcd_string(lcd, date_buffer);
	lcd_set_cursor(lcd, 0, 1);
	lcd_string(lcd, time_buffer);
}
#endif

void show_main_screen(struct ctx *ctx, struct tm *now)
{
	static char line1[17];
	static char line2[17];

	snprintf(line1, sizeof(line1), "%02d:%02d  %s  %s",
		 now->tm_hour, now->tm_min, DAY_STR[now->tm_wday], MODE_STR[ctx->mode]);

	line1[sizeof(line1) - 1] = '\0';
	snprintf(line2, sizeof(line2), " %02d:%02d - %02d:%02d",
		ctx->settings.day_begin.hour, ctx->settings.day_begin.minute,
		ctx->settings.day_end.hour, ctx->settings.day_end.minute
		);
	line2[sizeof(line2) - 1] = '\0';
	
	//lcd_clear(lcd);
	/*k_msleep(MSEC_PER_SEC * 3U);*/
	lcd_set_cursor(ctx->lcd, 0, 0);
	
	lcd_string(ctx->lcd, line1);
	lcd_set_cursor(ctx->lcd, 0, 1);
	lcd_string(ctx->lcd, line2);
}

void ctrl_button_handler(void* dev, enum button_type type, bool pressed)
{
	static struct msgq_item_t tx_data;
	static int64_t last_button_event = 0;
	if (pressed) {
		last_button_event = k_uptime_get();
	}
	tx_data.button_index = type;
	tx_data.button_pressed = pressed;
	tx_data.duration_msec = (uint32_t) k_uptime_delta(&last_button_event);
	last_button_event = k_uptime_get();
	LOG_INF("Button %d %s (%d ms)\n", type, pressed ? "pressed" : "released", tx_data.duration_msec);
	k_msgq_put(&ctrl_msgq, &tx_data, K_NO_WAIT);
}

static void ctrl_reset_screen(void) {
	void *lcd = ctrl_ctx.lcd;
	lcd_backlight(lcd, false);
	ctrl_ctx.cursor.col = 0;
	ctrl_ctx.cursor.row = 0;
	LOG_DBG("");
}

static void user_input_expiry_function(struct k_timer *timer_id)
{
	LOG_INF("Input timer expired");
	ctrl_reset_screen();
	ctrl_ctx.input_mode = INPUT_MODE_VIEW;
}

static enum op_mode calc_new_mode(struct ctrl_settings* settings, struct tm* now)
{
	LOG_DBG("");
	if (now->tm_hour < settings->day_begin.hour) {
		return OP_MODE_NIGHT;
	}
	if (now->tm_hour > settings->day_end.hour) {
		return OP_MODE_NIGHT;
	}
	if ((now->tm_hour == settings->day_begin.hour) &&
	    (now->tm_min < settings->day_begin.minute)) {
		return OP_MODE_NIGHT;
	}
	if ((now->tm_hour == settings->day_end.hour) &&
	    (now->tm_min > settings->day_end.minute)) {
		return OP_MODE_NIGHT;
	}

	return OP_MODE_DAY;
}

static void ctrl_set_cursor_pos(enum input_mode input_mode)
{
	void *lcd = ctrl_ctx.lcd;
	
	switch(input_mode) {
	case INPUT_MODE_VIEW:
	case INPUT_MODE_EDIT_CLOCK_HOUR:
		ctrl_ctx.cursor.row = 0;
		ctrl_ctx.cursor.col = 1;
		break;
	case INPUT_MODE_EDIT_CLOCK_MINUTE:
		ctrl_ctx.cursor.row = 0;
		ctrl_ctx.cursor.col = 4;
		break;	
	case INPUT_MODE_EDIT_DAY:
		ctrl_ctx.cursor.row = 0;
		ctrl_ctx.cursor.col = 7;
		break;
	case INPUT_MODE_EDIT_SCHEDULE_BEGIN_HOUR:
		ctrl_ctx.cursor.row = 1;
		ctrl_ctx.cursor.col = 2;
		break;
	case INPUT_MODE_EDIT_SCHEDULE_BEGIN_MINUTE:
		ctrl_ctx.cursor.row = 1;
		ctrl_ctx.cursor.col = 5;
		break;
	case INPUT_MODE_EDIT_SCHEDULE_END_HOUR:
		ctrl_ctx.cursor.row = 1;
		ctrl_ctx.cursor.col = 10;
		break;
	case INPUT_MODE_EDIT_SCHEDULE_END_MINUTE:
		ctrl_ctx.cursor.row = 1;
		ctrl_ctx.cursor.col = 13;
		break;
	case INPUT_MODE_LAST:
	default:
		ctrl_ctx.cursor.row = 0;
		ctrl_ctx.cursor.col = 0;
	}
	lcd_set_cursor(lcd, ctrl_ctx.cursor.col, ctrl_ctx.cursor.row);
}

static void ctrl_change_cap_hour(uint8_t* current, int8_t delta)
{
	int16_t new_value = *current + delta;

	if (new_value < 0) {
		new_value = 0;
	} else if (new_value > 23) {
		new_value = 23;
	}
	*current = new_value;
}

static void ctrl_change_cap_minute(uint8_t* current, int8_t delta)
{
	int16_t new_value = *current + delta;

	if (new_value < 0) {
		new_value = 0;
	} else if (new_value > 59) {
		new_value = 59;
	}
	*current = new_value;
}

static void ctrl_change_current_item(int8_t delta)
{
	struct tm now_set;
	struct tm* now;
	int new_val;
	
	switch(ctrl_ctx.input_mode) {
	case INPUT_MODE_VIEW:
		LOG_ERR("view mode, should never get here!!");
		return;
	case INPUT_MODE_EDIT_CLOCK_HOUR:
		LOG_INF("Change clock hour");
		now = clock_rtc_read(ctrl_ctx.clock);
		now_set = *now;
		new_val = now_set.tm_hour + delta;
		if (new_val < 0) {
			new_val = 0;
		} else if (new_val > 23) {
			new_val = 23;
		}
		now_set.tm_hour = new_val;
		clock_rtc_set(ctrl_ctx.clock, &now_set);
		break;
	case INPUT_MODE_EDIT_CLOCK_MINUTE:
		LOG_INF("Change clock minute");
		now = clock_rtc_read(ctrl_ctx.clock);
		now_set = *now;
		new_val = now_set.tm_min + delta;
		if (new_val < 0) {
			new_val = 0;
		} else if (new_val > 59) {
			new_val = 59;
		}
		now_set.tm_min = new_val;
		clock_rtc_set(ctrl_ctx.clock, &now_set);
		break;	
	case INPUT_MODE_EDIT_DAY:
		LOG_INF("Change clock day");
		now = clock_rtc_read(ctrl_ctx.clock);
		now_set = *now;
		new_val = now_set.tm_wday + delta;
		if (new_val < 0) {
			new_val = 6;
		} else if (new_val > 6) {
			new_val = 0;
		}
		now_set.tm_wday = new_val;
		clock_rtc_set(ctrl_ctx.clock, &now_set);
		break;
	case INPUT_MODE_EDIT_SCHEDULE_BEGIN_HOUR:
		LOG_INF("Change sched begin hour by %d", delta);
		ctrl_change_cap_hour(&ctrl_ctx.settings.day_begin.hour, delta);
		break;
	case INPUT_MODE_EDIT_SCHEDULE_BEGIN_MINUTE:
		LOG_INF("Change sched begin minute by %d", delta);
		ctrl_change_cap_minute(&ctrl_ctx.settings.day_begin.minute, delta);
		break;
	case INPUT_MODE_EDIT_SCHEDULE_END_HOUR:
		LOG_INF("Change sched end hour by %d", delta);
		ctrl_change_cap_hour(&ctrl_ctx.settings.day_end.hour, delta);
		break;
	case INPUT_MODE_EDIT_SCHEDULE_END_MINUTE:
		LOG_INF("Change sched end minute by %d", delta);
		ctrl_change_cap_minute(&ctrl_ctx.settings.day_end.minute, delta);
		break;
	case INPUT_MODE_LAST:
	default:
		break;
	}

	if ((ctrl_ctx.input_mode == INPUT_MODE_EDIT_SCHEDULE_BEGIN_HOUR) ||
	    (ctrl_ctx.input_mode == INPUT_MODE_EDIT_SCHEDULE_BEGIN_MINUTE) ||
	    (ctrl_ctx.input_mode == INPUT_MODE_EDIT_SCHEDULE_END_HOUR) ||
	    (ctrl_ctx.input_mode == INPUT_MODE_EDIT_SCHEDULE_END_MINUTE))
	{
		struct persistent_ctrl_settings settings = {
			.magic_no = PERSISTENT_SETTINGS_MAGIC,
			.settings = ctrl_ctx.settings
		};
		if (!clock_rtc_reg_write(&settings, sizeof(settings))) {
			LOG_ERR("Failed to persist settings in rtc regs");
		}
	}
}

static void ctrl_set_output_pins(void)
{
	LOG_INF("Setting output pins for mode %d", ctrl_ctx.mode);
	
	switch(ctrl_ctx.mode) {
	case OP_MODE_DAY:
		output_set(ctrl_ctx.output, OUTPUT_DAY);
		break;
	case OP_MODE_NIGHT:
		output_set(ctrl_ctx.output, OUTPUT_NIGHT);
		break;
	case OP_MODE_OFF:
	default:
		output_set(ctrl_ctx.output, OUTPUT_OFF);
		break;
	}
}

static void ctrl_func(void *ctx, void *u2, void *u3)
{
	void *lcd = ctrl_ctx.lcd;
	void *clock = ctrl_ctx.clock;
	//void *buttons = ctrl_ctx.buttons;
	struct msgq_item_t event;
	int res;
	struct tm *now;

	struct tm now_set = {
		.tm_sec = 30,
		.tm_min = 23,
		.tm_hour = 19,
		.tm_mday = 4,
		.tm_wday = 6,
		.tm_mon = 6,
		.tm_year = 120
	};


	//enable backlight
	lcd_backlight(lcd, true);

	/* Clear display */
	lcd_clear(lcd);
	/*k_msleep(MSEC_PER_SEC * 3U);*/
	lcd_set_cursor(lcd, 0, 0);
	lcd_string(lcd, "Heizungs-Ctrl");
	lcd_set_cursor(lcd, 0, 1);
	lcd_string(lcd, "V 2020-07-04");
	
	now = clock_rtc_read(clock);
	if (now->tm_year < 120) {
		//if rtc returns date before 2020, set clock to some hardcoded default
		clock_rtc_set(clock, &now_set);
	}

	k_msleep(MSEC_PER_SEC * 3U);
	ctrl_ctx.cursor.row = 0;
	ctrl_ctx.cursor.col = 0;

	k_timer_start(&user_input_timer, K_SECONDS(30), K_NO_WAIT);

	/* to unblock to init lcd and state */
	static struct msgq_item_t tx_data = {
		.button_index = BUTTON_NONE,
		.button_pressed = false,
	};
	k_msgq_put(&ctrl_msgq, &tx_data, K_NO_WAIT);
	
	while (1) {
		res = k_msgq_get(&ctrl_msgq, &event, K_MSEC(15000));

		struct tm* now = clock_rtc_read(clock);
		enum op_mode new_mode = calc_new_mode(&ctrl_ctx.settings, now);
		if (new_mode != ctrl_ctx.mode) {
			LOG_INF("Switching modes (%s -> %s)", MODE_STR[ctrl_ctx.mode], MODE_STR[new_mode]);
			ctrl_ctx.mode = new_mode;
			ctrl_set_output_pins();
		}
		
		if (res) {
			/* Clear display */
			lcd_clear(lcd);
			show_main_screen(&ctrl_ctx, now);
			continue;
		}
		
		if (!event.button_pressed) {
			LOG_INF("Restarting input timer");
			lcd_backlight(lcd, true);
			k_timer_start(&user_input_timer, K_SECONDS(30), K_NO_WAIT);
			// handle input
			switch (event.button_index) {
			case BUTTON_SELECT:
				/* so the select button was released */
				if ((event.duration_msec >= 3000) && (ctrl_ctx.input_mode == INPUT_MODE_VIEW)) {
					LOG_INF("Change into edit mode");
					ctrl_ctx.input_mode = INPUT_MODE_EDIT_CLOCK_HOUR;
				} else if (ctrl_ctx.input_mode != INPUT_MODE_VIEW) {
					LOG_INF("Change into view mode");
					ctrl_ctx.input_mode = INPUT_MODE_VIEW;
				}
				break;
			case BUTTON_RIGHT:
				if (ctrl_ctx.input_mode > INPUT_MODE_VIEW) {
					ctrl_ctx.input_mode++;
					if (ctrl_ctx.input_mode >= INPUT_MODE_LAST) {
						ctrl_ctx.input_mode = INPUT_MODE_EDIT_CLOCK_HOUR;
					}
				}
				break;
			case BUTTON_LEFT:
				if (ctrl_ctx.input_mode > INPUT_MODE_VIEW) {
					ctrl_ctx.input_mode--;
					if (ctrl_ctx.input_mode <= INPUT_MODE_VIEW) {
						ctrl_ctx.input_mode = INPUT_MODE_LAST - 1;
					}
				}
				break;
			case BUTTON_UP:
				if (ctrl_ctx.input_mode > INPUT_MODE_VIEW) {
					//change current setting
					ctrl_change_current_item(1);
				}
				break;
			case BUTTON_DOWN:
				if (ctrl_ctx.input_mode > INPUT_MODE_VIEW) {
					//change current setting
					ctrl_change_current_item(-1);
				}
				break;
				
			default:
				lcd_blink_off(lcd);				
			}
		}
		LOG_INF("1 - %d", ctrl_ctx.input_mode);
		if (!event.button_pressed) {
			/* Clear display */
			lcd_clear(lcd);
			now = clock_rtc_read(clock);
			show_main_screen(&ctrl_ctx, now);
			LOG_INF("3 - %d", ctrl_ctx.input_mode);
					
		}
		LOG_INF("2 - %d", ctrl_ctx.input_mode);

		if (ctrl_ctx.input_mode > INPUT_MODE_VIEW) {
			ctrl_set_cursor_pos(ctrl_ctx.input_mode);
			lcd_blink_on(lcd);
		} else {
			lcd_blink_off(lcd);
		}
		LOG_INF("4 - %d", ctrl_ctx.input_mode);
				
	}
}

void* ctrl_init(void)
{
	struct persistent_ctrl_settings read_settings;
	ctrl_ctx.input_mode = INPUT_MODE_VIEW;
	ctrl_ctx.settings.day_begin.hour = 6;
	ctrl_ctx.settings.day_begin.minute = 0;
	ctrl_ctx.settings.day_end.hour = 22;
	ctrl_ctx.settings.day_end.minute = 0;
	ctrl_ctx.mode = OP_MODE_OFF;
	ctrl_ctx.lcd = lcd_init();
	
	if (!ctrl_ctx.lcd) {
		LOG_ERR("Failed to init lcd\n");
		return NULL;
	}

	ctrl_ctx.buttons = buttons_init(ctrl_button_handler);

	if (!ctrl_ctx.buttons) {
		LOG_ERR("Failed to init button driver\n");
		return NULL;
	}

	ctrl_ctx.output = output_init();

	if (!ctrl_ctx.output) {
		LOG_ERR("Failed to init output driver\n");
		return NULL;
	}

	ctrl_ctx.clock = clock_init();

	if (!ctrl_ctx.clock) {
		LOG_ERR("Failed to init clock driver\n");
		return NULL;
	}

	if (!clock_rtc_reg_read(&read_settings, sizeof(read_settings))) {
		LOG_ERR("Failed read settings from rtc regs\n");
	} else {
		if (read_settings.magic_no != PERSISTENT_SETTINGS_MAGIC) {
			LOG_WRN("Settings from RTC wo magic, discarding");
		} else {
			ctrl_ctx.settings = read_settings.settings;
		}
	}

	k_thread_create(&ctrl_thread_data, ctrl_stack_area,
			K_THREAD_STACK_SIZEOF(ctrl_stack_area),
			ctrl_func,
			&ctrl_ctx, NULL, NULL,
			CTRL_THREAD_PRIORITY, 0, K_NO_WAIT);

	return &ctrl_ctx;
}
