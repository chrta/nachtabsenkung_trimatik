/*
 * Copyright (c) 2020 Christian Taedcke
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "clock.h"

#include <drivers/counter.h>
#include <soc.h>

#include <string.h>

#if defined(CONFIG_BOARD_NUCLEO_F429ZI) || defined(CONFIG_BOARD_NUCLEO_F446RE)
#define RTC_DEVICE_NAME         DT_LABEL(DT_INST(0, st_stm32_rtc))
#else
#error "Unsupported board"
#endif

#ifdef CONFIG_COUNTER_RTC_STM32
struct tm* clock_rtc_read(void* dev)
{
	static struct tm now = { 0 };
	uint32_t rtc_date, rtc_time;

	ARG_UNUSED(dev);
	
	/* Read time and date registers */
	rtc_time = LL_RTC_TIME_Get(RTC);
	rtc_date = LL_RTC_DATE_Get(RTC);

	/* Convert calendar datetime to UNIX timestamp */
	/* RTC start time: 1st, Jan, 2000 */
	/* time_t start:   1st, Jan, 1900 */
	now.tm_year = 100 +
			__LL_RTC_CONVERT_BCD2BIN(__LL_RTC_GET_YEAR(rtc_date));
	/* tm_mon allowed values are 0-11 */
	now.tm_mon = __LL_RTC_CONVERT_BCD2BIN(__LL_RTC_GET_MONTH(rtc_date)) - 1;
	now.tm_mday = __LL_RTC_CONVERT_BCD2BIN(__LL_RTC_GET_DAY(rtc_date));
	now.tm_wday = __LL_RTC_CONVERT_BCD2BIN(__LL_RTC_GET_WEEKDAY(rtc_date));
	if (now.tm_wday >= LL_RTC_WEEKDAY_SUNDAY)
	{
		now.tm_wday = 0;
	}

	now.tm_hour = __LL_RTC_CONVERT_BCD2BIN(__LL_RTC_GET_HOUR(rtc_time));
	now.tm_min = __LL_RTC_CONVERT_BCD2BIN(__LL_RTC_GET_MINUTE(rtc_time));
	now.tm_sec = __LL_RTC_CONVERT_BCD2BIN(__LL_RTC_GET_SECOND(rtc_time));

	return &now;
}

void clock_rtc_set(void *dev, const struct tm *now)
{
	ARG_UNUSED(now);

	LL_RTC_DisableWriteProtection(RTC);
	//BCD Format!!! 23 uhr = 0x23
	LL_RTC_EnableInitMode(RTC);

	k_msleep(100);
	LL_RTC_TIME_Config(RTC, LL_RTC_TIME_FORMAT_AM_OR_24,
			   __LL_RTC_CONVERT_BIN2BCD(now->tm_hour),
			   __LL_RTC_CONVERT_BIN2BCD(now->tm_min),
			   __LL_RTC_CONVERT_BIN2BCD(now->tm_sec));
	/* now->tm_year is since 1900 */
	LL_RTC_DATE_Config(RTC,
			   now->tm_wday == 0 ? LL_RTC_WEEKDAY_SUNDAY : now->tm_wday,
			   __LL_RTC_CONVERT_BIN2BCD(now->tm_mday),
			   __LL_RTC_CONVERT_BIN2BCD(now->tm_mon + 1),
			   __LL_RTC_CONVERT_BIN2BCD(now->tm_year - 100));
	LL_RTC_DisableInitMode(RTC);
	LL_RTC_EnableWriteProtection(RTC);
}

bool clock_rtc_reg_read(void* buffer, size_t len)
{
	if (len > 18 * 4) {
		printk("Trying to read too much data");
		return false;
	}
	uint32_t data;
	for (size_t i = 0; i < len; i += 4) {
		data = LL_RTC_BAK_GetRegister(RTC, LL_RTC_BKP_DR0 + (i / 4));
		memcpy((uint8_t*)buffer + i, &data, (len - i) >= 4 ? 4 : len - i);
	}
	return true;

}

bool clock_rtc_reg_write(const void* buffer, size_t len)
{
	if (len > 18 * 4) {
		printk("Trying to store too much data");
		return false;
	}
	uint32_t data = 0;
	for (size_t i = 0; i < len; i +=4) {
		data = 0;
		memcpy(&data, (const uint8_t*) buffer + i, (len - i) >= 4 ? 4 : len - i);
		LL_RTC_BAK_SetRegister(RTC, LL_RTC_BKP_DR0 + (i / 4), data);
	}
	return true;
}

#endif

void* clock_init(void)
{
	struct device *rtc_dev = device_get_binding(RTC_DEVICE_NAME);

	if (!rtc_dev) {
		printk("Failed to get adc dev\n");
		return NULL;
	}

	return rtc_dev;
}
