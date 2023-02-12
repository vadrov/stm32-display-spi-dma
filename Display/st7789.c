/*
 *	Драйвер управления дисплеями по SPI
 *  Author: VadRov
 *  Copyright (C) 2019 - 2022, VadRov, all right reserved.
 *
 *  Допускается свободное распространение без целей коммерческого использования.
 *  При коммерческом использовании необходимо согласование с автором.
 *  Распространятся по типу "как есть", то есть использование осуществляете на свой страх и риск.
 *  Автор не предоставляет никаких гарантий.
 *
 *  Версия: 1.4 (на CMSIS и LL) для STM32F4
 *
 *  https://www.youtube.com/@VadRov
 *  https://dzen.ru/vadrov
 *  https://vk.com/vadrov
 *  https://t.me/vadrov_channel
 */

#include "../Display/st7789.h"

#include "main.h"

#include "../Display/display.h"

uint8_t st7789_init_str[] = {
			LCD_UPR_COMMAND, ST7789_SWRESET, 0,
			LCD_UPR_PAUSE, 5,
			LCD_UPR_COMMAND, ST7789_DISPOFF, 0,
			LCD_UPR_COMMAND, ST7789_COLMOD, 1, ST7789_COLOR_MODE_16bit,
			LCD_UPR_COMMAND, ST7789_PORCTRL, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
			LCD_UPR_COMMAND, ST7789_MADCTL, 1, 0, //mem_config (memory data access config)
			LCD_UPR_COMMAND, ST7789_GCTRL, 1, 0x35,
		    LCD_UPR_COMMAND, ST7789_VCOMS, 1, 0x19,
		    LCD_UPR_COMMAND, ST7789_LCMCTRL, 1, 0x2C,
		    LCD_UPR_COMMAND, ST7789_VDVVRHEN, 2, 0x01, 0xFF,
		    LCD_UPR_COMMAND, ST7789_VRHS, 1, 0x12,
		    LCD_UPR_COMMAND, ST7789_VDVS, 1, 0x20,
		    LCD_UPR_COMMAND, ST7789_FRCTRL2, 1, 0x0F,
		    LCD_UPR_COMMAND, ST7789_PWCTRL1, 2, 0xA4, 0xA1,
			LCD_UPR_COMMAND, ST7789_PVGAMCTRL, 14, 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23,
			LCD_UPR_COMMAND, ST7789_NVGAMCTRL, 14, 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23,
			LCD_UPR_COMMAND, ST7789_INVON, 0,
			LCD_UPR_COMMAND, ST7789_SLPOUT, 0,
			LCD_UPR_PAUSE, 5,
			LCD_UPR_COMMAND, ST7789_NORON, 0,
			LCD_UPR_COMMAND, ST7789_DISPON, 0,
			LCD_UPR_PAUSE, 120,
			LCD_UPR_END
	};

uint8_t	st7789_setwin_str[]  = {	LCD_UPR_COMMAND, ST7789_CASET, 4, 0, 0, 0, 0,	//x0, x1
								 	LCD_UPR_COMMAND, ST7789_RASET, 4, 0, 0, 0, 0,	//y0, y1
									LCD_UPR_COMMAND, ST7789_RAMWR, 0,
									LCD_UPR_END
								};

uint8_t st7789_sleepin_str[]  = {	LCD_UPR_COMMAND, ST7789_SLPIN, 0,
									LCD_UPR_PAUSE, 10,
									LCD_UPR_END
								};

uint8_t st7789_sleepout_str[] = {	LCD_UPR_COMMAND,
									ST7789_SLPOUT, 0,
									LCD_UPR_PAUSE, 120,
									LCD_UPR_END
								};

static uint8_t ST7789_MemoryDataAccessControlConfig(uint8_t mirror_x, uint8_t mirror_y, uint8_t exchange_xy, uint8_t mirror_color, uint8_t refresh_v, uint8_t refresh_h)
{
	uint8_t mem_config = 0;
	if (mirror_x) 		mem_config |= ST7789_MADCTL_MX;
	if (mirror_y) 		mem_config |= ST7789_MADCTL_MY;
	if (exchange_xy) 	mem_config |= ST7789_MADCTL_MV;
	if (mirror_color) 	mem_config |= ST7789_MADCTL_BGR;
	if (refresh_v)		mem_config |= ST7789_MADCTL_ML;
	if (refresh_h)		mem_config |= ST7789_MADCTL_MH;
	return mem_config;
}

uint8_t* ST7789_Init(uint8_t orientation)
{
	uint8_t mem_config = 0;
	if (orientation == PAGE_ORIENTATION_PORTRAIT) 				mem_config = ST7789_MemoryDataAccessControlConfig(0, 0, 0, 0, 0, 0);
	else if (orientation == PAGE_ORIENTATION_PORTRAIT_MIRROR) 	mem_config = ST7789_MemoryDataAccessControlConfig(1, 1, 0, 0, 1, 1);
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE) 		mem_config = ST7789_MemoryDataAccessControlConfig(1, 0, 1, 0, 0, 1);
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE_MIRROR) 	mem_config = ST7789_MemoryDataAccessControlConfig(0, 1, 1, 0, 1, 0);
	st7789_init_str[23] = mem_config;
	return st7789_init_str;
}

uint8_t* ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	st7789_setwin_str[3] = x0 >> 8; 	st7789_setwin_str[4] = x0 & 0xFF;
	st7789_setwin_str[5] = x1 >> 8; 	st7789_setwin_str[6] = x1 & 0xFF;
	st7789_setwin_str[10] = y0 >> 8; 	st7789_setwin_str[11] = y0 & 0xFF;
	st7789_setwin_str[12] = y1 >> 8; 	st7789_setwin_str[13] = y1 & 0xFF;
	return st7789_setwin_str;
}

uint8_t* ST7789_SleepIn(void)
{
	return st7789_sleepin_str;
}

uint8_t* ST7789_SleepOut(void)
{
	return st7789_sleepout_str;
}
