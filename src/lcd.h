/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_LCD_H
#define APP_LCD_H

#include <zephyr.h>

void* lcd_init(void);

void lcd_backlight(void* lcd, bool enable);

void lcd_clear(void* lcd);

/** Set curson position */
void lcd_set_cursor(void* lcd, uint8_t col, uint8_t row);

void lcd_string(void* lcd, const char *msg);

void lcd_scroll_right(void *lcd);

void lcd_scroll_left(void *lcd);

void lcd_cursor_on(void *lcd);

void lcd_cursor_off(void *lcd);

void lcd_blink_on(void *lcd);

void lcd_blink_off(void *lcd);


#endif /* APP_LCD_H */
