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

#include "../Display/ili9341.h"

#include "main.h"

#include "../Display/display.h"

uint8_t ili9341_init_str[] = {
			LCD_UPR_COMMAND, ILI9341_SOFTRESET, 0,
			LCD_UPR_PAUSE, 5,
			LCD_UPR_COMMAND, ILI9341_DISPLAYOFF, 0,
	 		LCD_UPR_COMMAND, ILI9341_POWERCONTROLB, 4, 0, 0x00, 0x83, 0x30,
			LCD_UPR_COMMAND, ILI9341_POWERSEQCONTROL, 4, 0x64, 0x03, 0x12, 0x81,
			LCD_UPR_COMMAND, ILI9341_DRIVERTIMCONTROLA, 3, 0x85, 0x01, 0x79,
			LCD_UPR_COMMAND, ILI9341_POWERCONTROLA, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
			LCD_UPR_COMMAND, ILI9341_PUMPRATIOCONTROL, 1, 0x20,
			LCD_UPR_COMMAND, ILI9341_DRIVERTIMCONTROLC, 2, 0x00, 0x00,
			LCD_UPR_COMMAND, ILI9341_POWERCONTROL1, 1, 0x26,
			LCD_UPR_COMMAND, ILI9341_POWERCONTROL2, 1, 0x11,
			LCD_UPR_COMMAND, ILI9341_VCOMCONTROL1, 2, 0x35, 0x3E,
			LCD_UPR_COMMAND, ILI9341_VCOMCONTROL2, 1, 0xBE,
			LCD_UPR_COMMAND, ILI9341_MEMCONTROL, 1, 0,  //mem_config (memory data access config)
			LCD_UPR_COMMAND, ILI9341_PIXELFORMAT, 1, ILI9341_COLOR_MODE_16bit,
			LCD_UPR_COMMAND, ILI9341_FRAMECONTROLNORMAL, 2, 0x00, 0x1B,
			LCD_UPR_COMMAND, ILI9341_ENABLE3G, 1, 0x08,
			LCD_UPR_COMMAND, ILI9341_GAMMASET, 1, 0x01,
			LCD_UPR_COMMAND, ILI9341_POSITIVEGAMMCORR, 15, 0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0x87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00,
			LCD_UPR_COMMAND, ILI9341_NEGATIVEGAMMCORR, 15, 0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F,
			LCD_UPR_COMMAND, ILI9341_ENTRYMODE, 1, 0x07,
			LCD_UPR_COMMAND, ILI9341_DISPLAYFUNC, 4, 0x0A, 0x82, 0x27, 0x00,
			LCD_UPR_COMMAND, ILI9341_TEARINGEFFECTON, 1, 1,
			LCD_UPR_COMMAND, ILI9341_SLEEPOUT, 0,
			LCD_UPR_PAUSE, 5,
			LCD_UPR_COMMAND, ILI9341_DISPLAYON, 0,
			LCD_UPR_PAUSE, 120,
			LCD_UPR_END
		};

uint8_t	ili9341_setwin_str[]  = {
									LCD_UPR_COMMAND, ILI9341_COLADDRSET,  4, 0, 0, 0, 0,	//x0, x1
								 	LCD_UPR_COMMAND, ILI9341_PAGEADDRSET, 4, 0, 0, 0, 0,	//y0, y1
									LCD_UPR_COMMAND, ILI9341_MEMORYWRITE, 0,
									LCD_UPR_END
								};

uint8_t ili9341_sleepin_str[]  = {
									LCD_UPR_COMMAND, ILI9341_SLEEPIN, 0,
									LCD_UPR_PAUSE, 10,
									LCD_UPR_END
								};

uint8_t ili9341_sleepout_str[] = {
									LCD_UPR_COMMAND, ILI9341_SLEEPOUT, 0,
									LCD_UPR_PAUSE, 120,
									LCD_UPR_END
								};

static uint8_t ILI9341_MemoryDataAccessControlConfig(uint8_t mirror_x, uint8_t mirror_y, uint8_t exchange_xy, uint8_t mirror_color, uint8_t refresh_v, uint8_t refresh_h)
{
	uint8_t mem_config = 0;
	if (mirror_x)		mem_config |= ILI9341_MADCTL_MX;
	if (mirror_y)		mem_config |= ILI9341_MADCTL_MY;
	if (exchange_xy)	mem_config |= ILI9341_MADCTL_MV;
	if (mirror_color)	mem_config |= ILI9341_MADCTL_BGR;
	if (refresh_v)		mem_config |= ILI9341_MADCTL_ML;
	if (refresh_h)		mem_config |= ILI9341_MADCTL_MH;
	return mem_config;
}

uint8_t* ILI9341_Init(uint8_t orientation)
{
	uint8_t mem_config = 0;
	if (orientation == PAGE_ORIENTATION_PORTRAIT) 				mem_config = ILI9341_MemoryDataAccessControlConfig(1, 0, 0, 1, 0, 0);
	else if (orientation == PAGE_ORIENTATION_PORTRAIT_MIRROR) 	mem_config = ILI9341_MemoryDataAccessControlConfig(0, 1, 0, 1, 1, 1);
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE) 		mem_config = ILI9341_MemoryDataAccessControlConfig(0, 0, 1, 1, 0, 1);
	else if (orientation == PAGE_ORIENTATION_LANDSCAPE_MIRROR) 	mem_config = ILI9341_MemoryDataAccessControlConfig(1, 1, 1, 1, 1, 0);
	ili9341_init_str[65] = mem_config;
	return ili9341_init_str;
}

uint8_t* ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	ili9341_setwin_str[3] = x0 >> 8;  ili9341_setwin_str[4] = x0 & 0xFF;
	ili9341_setwin_str[5] = x1 >> 8;  ili9341_setwin_str[6] = x1 & 0xFF;
	ili9341_setwin_str[10] = y0 >> 8; ili9341_setwin_str[11] = y0 & 0xFF;
	ili9341_setwin_str[12] = y1 >> 8; ili9341_setwin_str[13] = y1 & 0xFF;
	return ili9341_setwin_str;
}

uint8_t* ILI9341_SleepIn(void)
{
	return ili9341_sleepin_str;
}

uint8_t* ILI9341_SleepOut(void)
{
	return ili9341_sleepout_str;
}
