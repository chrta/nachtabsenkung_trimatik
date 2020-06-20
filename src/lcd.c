/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lcd.h"

#include <sys/printk.h>
#include <drivers/gpio.h>
#include <string.h>

#if defined(CONFIG_BOARD_NUCLEO_F429ZI)
/*	https://wiki.dfrobot.com/Arduino_LCD_KeyPad_Shield__SKU__DFR0009_ */
/* Define GPIO OUT to LCD */
#define GPIO_PIN_D4		14	/* DB4 PF14 */
#define GPIO_PORT_D4	        "GPIOF"
#define GPIO_PIN_D5		11	/* DB5 PE11 */
#define GPIO_PORT_D5	        "GPIOE"
#define GPIO_PIN_D6		9	/* DB6 PE9 */
#define GPIO_PORT_D6	        "GPIOE"
#define GPIO_PIN_D7		13	/* DB7 PF13 */
#define GPIO_PORT_D7	        "GPIOF"
#define GPIO_PIN_RS		12	/* D8 RS PF12 */
#define GPIO_PORT_RS	        "GPIOF"
#define GPIO_PIN_E		15	/* D9 Enable PD15 */
#define GPIO_PORT_E	        "GPIOD"
#define GPIO_PIN_BL       14               /* D10 backlight control PD14 */
#define GPIO_PORT_BL	        "GPIOD"
#elif defined(CONFIG_BOARD_NUCLEO_F446RE)
#define GPIO_PIN_D4		5	/* DB4 PB5 */
#define GPIO_PORT_D4	        "GPIOB"
#define GPIO_PIN_D5		4	/* DB5 PB4 */
#define GPIO_PORT_D5	        "GPIOB"
#define GPIO_PIN_D6		10	/* DB6 PB10 */
#define GPIO_PORT_D6	        "GPIOB"
#define GPIO_PIN_D7		8	/* DB7 PA8 */
#define GPIO_PORT_D7	        "GPIOA"
#define GPIO_PIN_RS		9	/* D8 RS PA9 */
#define GPIO_PORT_RS	        "GPIOA"
#define GPIO_PIN_E		7	/* D9 Enable PC7 */
#define GPIO_PORT_E	        "GPIOC"
#define GPIO_PIN_BL       6               /* D10 backlight control PB6 */
#define GPIO_PORT_BL	        "GPIOB"
#else
#error "unsupported board"
#endif

struct gpio_info {
	const char* port;
	const uint8_t index;
	struct device *dev;
};

enum gpio_index {
	GPIO_IDX_D4 = 0,
	GPIO_IDX_D5,
	GPIO_IDX_D6,
	GPIO_IDX_D7,
	GPIO_IDX_RS,
	GPIO_IDX_E,
	GPIO_IDX_BL,
};

static struct gpio_info global_gpios[] = {
	{GPIO_PORT_D4, GPIO_PIN_D4, NULL},
	{GPIO_PORT_D5, GPIO_PIN_D5, NULL},
	{GPIO_PORT_D6, GPIO_PIN_D6, NULL},
	{GPIO_PORT_D7, GPIO_PIN_D7, NULL},
	{GPIO_PORT_RS, GPIO_PIN_RS, NULL},
	{GPIO_PORT_E, GPIO_PIN_E, NULL},
	{GPIO_PORT_BL, GPIO_PIN_BL, NULL},
};

/* Commands */
#define LCD_CLEAR_DISPLAY		0x01
#define LCD_RETURN_HOME			0x02
#define LCD_ENTRY_MODE_SET		0x04
#define LCD_DISPLAY_CONTROL		0x08
#define LCD_CURSOR_SHIFT		0x10
#define LCD_FUNCTION_SET		0x20
#define LCD_SET_CGRAM_ADDR		0x40
#define LCD_SET_DDRAM_ADDR		0x80

/* Display entry mode */
#define LCD_ENTRY_RIGHT			0x00
#define LCD_ENTRY_LEFT			0x02
#define LCD_ENTRY_SHIFT_INCREMENT	0x01
#define LCD_ENTRY_SHIFT_DECREMENT	0x00

/* Display on/off control */
#define LCD_DISPLAY_ON			0x04
#define LCD_DISPLAY_OFF			0x00
#define LCD_CURSOR_ON			0x02
#define LCD_CURSOR_OFF			0x00
#define LCD_BLINK_ON			0x01
#define LCD_BLINK_OFF			0x00

/* Display/cursor shift */
#define LCD_DISPLAY_MOVE		0x08
#define LCD_CURSOR_MOVE			0x00
#define LCD_MOVE_RIGHT			0x04
#define LCD_MOVE_LEFT			0x00

/* Function set */
#define LCD_8BIT_MODE			0x10
#define LCD_4BIT_MODE			0x00
#define LCD_2_LINE			0x08
#define LCD_1_LINE			0x00
#define LCD_5x10_DOTS			0x04
#define LCD_5x8_DOTS			0x00

/* Define some device constants */
#define LCD_WIDTH			20	/* Max char per line */
#define HIGH				1
#define LOW				0
/* in millisecond */
#define	ENABLE_DELAY			1/*10*/


struct pi_lcd_data {
	uint8_t	disp_func;	/* Display Function */
	uint8_t	disp_cntl;	/* Display Control */
	uint8_t disp_mode;	/* Display Mode */
	uint8_t	cfg_rows;
	uint8_t	row_offsets[4];
};

/* Default Configuration - User can update */
struct pi_lcd_data lcd_data = {
	.disp_func = LCD_4BIT_MODE | LCD_2_LINE | LCD_5x8_DOTS,
	.disp_cntl = 0,
	.disp_mode = 0,
	.cfg_rows = 0,
	.row_offsets = {0x00, 0x00, 0x00, 0x00}
};

void _set_row_offsets(int8_t row0, int8_t row1, int8_t row2, int8_t row3)
{
	lcd_data.row_offsets[0] = row0;
	lcd_data.row_offsets[1] = row1;
	lcd_data.row_offsets[2] = row2;
	lcd_data.row_offsets[3] = row3;
}

static inline void lcd_gpio_write(struct gpio_info* gpios, enum gpio_index idx, int value)
{
	struct gpio_info *gi = &gpios[idx];
	if (gpio_pin_set_raw(gi->dev, gi->index, value)) {
		printk("Failed to set idx %d to %d\n", idx, value);
	}
}

void _pi_lcd_toggle_enable(struct gpio_info* gpios)
{
	lcd_gpio_write(gpios, GPIO_IDX_E, LOW);
	k_msleep(ENABLE_DELAY);
	lcd_gpio_write(gpios, GPIO_IDX_E, HIGH);
	k_msleep(ENABLE_DELAY);
	lcd_gpio_write(gpios, GPIO_IDX_E, LOW);
	k_msleep(ENABLE_DELAY);
}


void _pi_lcd_4bits_wr(struct gpio_info* gpios, uint8_t bits)
{
	/* High bits */
	lcd_gpio_write(gpios, GPIO_IDX_D4, LOW);
	lcd_gpio_write(gpios, GPIO_IDX_D5, LOW);
	lcd_gpio_write(gpios, GPIO_IDX_D6, LOW);
	lcd_gpio_write(gpios, GPIO_IDX_D7, LOW);
	
	if ((bits & BIT(4)) == BIT(4)) {
		lcd_gpio_write(gpios, GPIO_IDX_D4, HIGH);
	}
	if ((bits & BIT(5)) == BIT(5)) {
		lcd_gpio_write(gpios, GPIO_IDX_D5, HIGH);
	}
	if ((bits & BIT(6)) == BIT(6)) {
		lcd_gpio_write(gpios, GPIO_IDX_D6, HIGH);
	}
	if ((bits & BIT(7)) == BIT(7)) {
		lcd_gpio_write(gpios, GPIO_IDX_D7, HIGH);
	}

	/* Toggle 'Enable' pin */
	_pi_lcd_toggle_enable(gpios);

	/* Low bits */
	lcd_gpio_write(gpios, GPIO_IDX_D4, LOW);
	lcd_gpio_write(gpios, GPIO_IDX_D5, LOW);
	lcd_gpio_write(gpios, GPIO_IDX_D6, LOW);
	lcd_gpio_write(gpios, GPIO_IDX_D7, LOW);

	if ((bits & BIT(0)) == BIT(0)) {
		lcd_gpio_write(gpios, GPIO_IDX_D4, HIGH);
	}
	if ((bits & BIT(1)) == BIT(1)) {
		lcd_gpio_write(gpios, GPIO_IDX_D5, HIGH);
	}
	if ((bits & BIT(2)) == BIT(2)) {
		lcd_gpio_write(gpios, GPIO_IDX_D6, HIGH);
	}
	if ((bits & BIT(3)) == BIT(3)) {
		lcd_gpio_write(gpios, GPIO_IDX_D7, HIGH);
	}

	/* Toggle 'Enable' pin */
	_pi_lcd_toggle_enable(gpios);
}

void _pi_lcd_8bits_wr(struct gpio_info* gpios, uint8_t bits)
{
#if 0
	/* High bits */
	GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC21_D7, LOW);
	GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC22_D6, LOW);
	GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC23_D5, LOW);
	GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC24_D4, LOW);
	GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC15_D3, LOW);
	GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC14_D2, LOW);
	GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC13_D1, LOW);
	GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC12_D0, LOW);

	/* Low bits */
	if ((bits & BIT(0)) == BIT(0)) {
		GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC12_D0, HIGH);
	}
	if ((bits & BIT(1)) == BIT(1)) {
		GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC13_D1, HIGH);
	}
	if ((bits & BIT(2)) == BIT(2)) {
		GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC14_D2, HIGH);
	}
	if ((bits & BIT(3)) == BIT(3)) {
		GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC15_D3, HIGH);
	}
	if ((bits & BIT(4)) == BIT(4)) {
		GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC24_D4, HIGH);
	}
	if ((bits & BIT(5)) == BIT(5)) {
		GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC23_D5, HIGH);
	}
	if ((bits & BIT(6)) == BIT(6)) {
		GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC22_D6, HIGH);
	}
	if ((bits & BIT(7)) == BIT(7)) {
		GPIO_PIN_WR(gpio_dev, GPIO_PIN_PC21_D7, HIGH);
	}

	/* Toggle 'Enable' pin */
	_pi_lcd_toggle_enable(gpio_dev);
#endif
}

void _pi_lcd_data(struct gpio_info* gpios, uint8_t bits)
{
	if (lcd_data.disp_func & LCD_8BIT_MODE) {
#if 0
		_pi_lcd_8bits_wr(gpios, bits);
#else
		printk("8 Bit mode not implemented\n\n");
#endif
	} else {
		_pi_lcd_4bits_wr(gpios, bits);
	}
}

void _pi_lcd_command(struct gpio_info* gpios, uint8_t bits)
{
	/* mode = False for command */
	lcd_gpio_write(gpios, GPIO_IDX_RS, LOW);
	_pi_lcd_data(gpios, bits);
}

void _pi_lcd_write(struct gpio_info* gpios, uint8_t bits)
{
	/* mode = True for character */
	lcd_gpio_write(gpios, GPIO_IDX_RS, HIGH);
	_pi_lcd_data(gpios, bits);
}


/*************************
 * USER can use these APIs
 *************************/
/** Home */
void pi_lcd_home(struct gpio_info* gpios)
{
	_pi_lcd_command(gpios, LCD_RETURN_HOME);
	k_sleep(K_MSEC(2));			/* wait for 2ms */
}

/** Set curson position */
void pi_lcd_set_cursor(struct gpio_info* gpios, uint8_t col, uint8_t row)
{
	size_t max_lines;

	max_lines = ARRAY_SIZE(lcd_data.row_offsets);
	if (row >= max_lines) {
		row = max_lines - 1;	/* Count rows starting w/0 */
	}
	if (row >= lcd_data.cfg_rows) {
		row = lcd_data.cfg_rows - 1;    /* Count rows starting w/0 */
	}
	_pi_lcd_command(gpios, (LCD_SET_DDRAM_ADDR | (col + lcd_data.row_offsets[row])));
}


/** Clear display */
void pi_lcd_clear(struct gpio_info* gpios)
{
	_pi_lcd_command(gpios, LCD_CLEAR_DISPLAY);
	k_sleep(K_MSEC(2));			/* wait for 2ms */
}


/** Display ON */
void pi_lcd_display_on(struct gpio_info* gpios)
{
	lcd_data.disp_cntl |= LCD_DISPLAY_ON;
	_pi_lcd_command(gpios,
			LCD_DISPLAY_CONTROL | lcd_data.disp_cntl);
}

/** Display OFF */
void pi_lcd_display_off(struct gpio_info* gpios)
{
	lcd_data.disp_cntl &= ~LCD_DISPLAY_ON;
	_pi_lcd_command(gpios,
			LCD_DISPLAY_CONTROL | lcd_data.disp_cntl);
}


/** Turns cursor off */
void pi_lcd_cursor_off(struct gpio_info* gpios)
{
	lcd_data.disp_cntl &= ~LCD_CURSOR_ON;
	_pi_lcd_command(gpios,
			LCD_DISPLAY_CONTROL | lcd_data.disp_cntl);
}

/** Turn cursor on */
void pi_lcd_cursor_on(struct gpio_info* gpios)
{
	lcd_data.disp_cntl |= LCD_CURSOR_ON;
	_pi_lcd_command(gpios,
			LCD_DISPLAY_CONTROL | lcd_data.disp_cntl);
}


/** Turn off the blinking cursor */
void pi_lcd_blink_off(struct gpio_info* gpios)
{
	lcd_data.disp_cntl &= ~LCD_BLINK_ON;
	_pi_lcd_command(gpios,
			LCD_DISPLAY_CONTROL | lcd_data.disp_cntl);
}

/** Turn on the blinking cursor */
void pi_lcd_blink_on(struct gpio_info* gpios)
{
	lcd_data.disp_cntl |= LCD_BLINK_ON;
	_pi_lcd_command(gpios,
			LCD_DISPLAY_CONTROL | lcd_data.disp_cntl);
}

/** Scroll the display left without changing the RAM */
void pi_lcd_scroll_left(struct gpio_info* gpios)
{
	_pi_lcd_command(gpios, LCD_CURSOR_SHIFT |
			LCD_DISPLAY_MOVE | LCD_MOVE_LEFT);
}

/** Scroll the display right without changing the RAM */
void pi_lcd_scroll_right(struct gpio_info* gpios)
{
	_pi_lcd_command(gpios, LCD_CURSOR_SHIFT |
			LCD_DISPLAY_MOVE | LCD_MOVE_RIGHT);
}

/** Text that flows from left to right */
void pi_lcd_left_to_right(struct gpio_info* gpios)
{
	lcd_data.disp_mode |= LCD_ENTRY_LEFT;
	_pi_lcd_command(gpios,
			LCD_ENTRY_MODE_SET | lcd_data.disp_cntl);
}

/** Text that flows from right to left */
void pi_lcd_right_to_left(struct gpio_info* gpios)
{
	lcd_data.disp_mode &= ~LCD_ENTRY_LEFT;
	_pi_lcd_command(gpios,
			LCD_ENTRY_MODE_SET | lcd_data.disp_cntl);
}

/** Right justify text from the cursor location */
void pi_lcd_auto_scroll_right(struct gpio_info* gpios)
{
	lcd_data.disp_mode |= LCD_ENTRY_SHIFT_INCREMENT;
	_pi_lcd_command(gpios,
			LCD_ENTRY_MODE_SET | lcd_data.disp_cntl);
}

/** Left justify text from the cursor location */
void pi_lcd_auto_scroll_left(struct gpio_info* gpios)
{
	lcd_data.disp_mode &= ~LCD_ENTRY_SHIFT_INCREMENT;
	_pi_lcd_command(gpios,
			LCD_ENTRY_MODE_SET | lcd_data.disp_cntl);
}

void pi_lcd_string(struct gpio_info* gpios, const char *msg)
{
	int i;
	int len = 0;
	uint8_t data;

	len = strlen(msg);
	if (len > LCD_WIDTH) {
		printk("Too long message! len %d %s\n", len, msg);
	}

	for (i = 0; i < len; i++) {
		data = msg[i];
		_pi_lcd_write(gpios, data);
	}
}


/** LCD initialization function */
void pi_lcd_init(struct gpio_info *gpios, uint8_t cols, uint8_t rows, uint8_t dotsize)
{
	if (rows > 1) {
		lcd_data.disp_func |= LCD_2_LINE;
	}
	lcd_data.cfg_rows = rows;

	_set_row_offsets(0x00, 0x40, 0x00 + cols, 0x40 + cols);

	/* For 1 line displays, a 10 pixel high font looks OK */
	if ((dotsize != LCD_5x8_DOTS) && (rows == 1U)) {
		lcd_data.disp_func |= LCD_5x10_DOTS;
	}

	/* SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
	 * according to datasheet, we need at least 40ms after power rises
	 * above 2.7V before sending commands. Arduino can turn on way
	 * before 4.5V so we'll wait 50
	 */
	k_sleep(K_MSEC(50 * 2));

	/* this is according to the hitachi HD44780 datasheet
	 * figure 23/24, pg 45/46 try to set 4/8 bits mode
	 */
	if (lcd_data.disp_func & LCD_8BIT_MODE) {
#if 0
		/* 1st try */
		_pi_lcd_command(gpios, 0x30);
		k_sleep(K_MSEC(5));			/* wait for 5ms */

		/* 2nd try */
		_pi_lcd_command(gpios, 0x30);
		k_sleep(K_MSEC(5));			/* wait for 5ms */

		/* 3rd try */
		_pi_lcd_command(gpios, 0x30);
		k_sleep(K_MSEC(1));			/* wait for 1ms */

		/* Set 4bit interface */
		_pi_lcd_command(gpios, 0x30);
#else
		printk("8Bit mode not impl\n\n");
#endif
	} else {
		/* 1st try */
		_pi_lcd_command(gpios, 0x03);
		k_sleep(K_MSEC(5 * 2));			/* wait for 5ms */

		/* 2nd try */
		_pi_lcd_command(gpios, 0x03);
		k_sleep(K_MSEC(5 * 2));			/* wait for 5ms */

		/* 3rd try */
		_pi_lcd_command(gpios, 0x03);
		k_sleep(K_MSEC(1 * 3));			/* wait for 1ms */

		/* Set 4bit interface */
		_pi_lcd_command(gpios, 0x02);
	}

	/* finally, set # lines, font size, etc. */
	_pi_lcd_command(gpios, (LCD_FUNCTION_SET | lcd_data.disp_func));

	/* turn the display on with no cursor or blinking default */
	lcd_data.disp_cntl = LCD_DISPLAY_ON | LCD_CURSOR_OFF | LCD_BLINK_OFF;
	pi_lcd_display_on(gpios);

	/* clear it off */
	pi_lcd_clear(gpios);

	/* Initialize to default text direction */
	lcd_data.disp_mode = LCD_ENTRY_LEFT | LCD_ENTRY_SHIFT_DECREMENT;
	/* set the entry mode */
	_pi_lcd_command(gpios, LCD_ENTRY_MODE_SET | lcd_data.disp_mode);
}

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

void *lcd_init(void)
{
	struct gpio_info* gpios = global_gpios;
	int ret;

	ret = gpio_init(gpios, ARRAY_SIZE(global_gpios));
	
	if (!ret) {
		printk("Failed to init lcd gpios\n");
		return NULL;
	}

	printk("LCD Init\n");
	pi_lcd_init(gpios, 16, 2, LCD_5x8_DOTS);

	return gpios;
}

void lcd_backlight(void* lcd, bool enable)
{
	struct gpio_info* gpios = lcd;

	lcd_gpio_write(gpios, GPIO_IDX_BL, enable ? HIGH : LOW);
}

void lcd_clear(void* lcd)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_clear(gpios);
}

void lcd_set_cursor(void* lcd, uint8_t col, uint8_t row)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_set_cursor(gpios, col, row);
}

void lcd_string(void* lcd, const char *msg)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_string(gpios, msg);
}

void lcd_scroll_right(void *lcd)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_scroll_right(gpios);
}


void lcd_scroll_left(void *lcd)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_scroll_right(gpios);
}

void lcd_cursor_off(void *lcd)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_cursor_off(gpios);
}

void lcd_cursor_on(void *lcd)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_cursor_on(gpios);
}

void lcd_blink_off(void *lcd)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_blink_off(gpios);
}

void lcd_blink_on(void *lcd)
{
	struct gpio_info* gpios = lcd;
	pi_lcd_blink_on(gpios);
}

