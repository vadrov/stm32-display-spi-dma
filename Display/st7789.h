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

#ifndef __ST7789_H
#define __ST7789_H

#include "main.h"

#define ST7789_CONTROLLER_WIDTH		240
#define ST7789_CONTROLLER_HEIGHT	320

//команды контроллера ST7789
#define ST7789_NOP     		0x00
#define ST7789_SWRESET 		0x01
#define ST7789_RDDID   		0x04
#define ST7789_RDDST   		0x09
#define ST7789_SLPIN   		0x10
#define ST7789_SLPOUT  		0x11
#define ST7789_PTLON   		0x12
#define ST7789_NORON   		0x13
#define ST7789_INVOFF  		0x20
#define ST7789_INVON   		0x21
#define ST7789_DISPOFF 		0x28
#define ST7789_DISPON  		0x29
#define ST7789_CASET   		0x2A
#define ST7789_RASET   		0x2B
#define ST7789_RAMWR   		0x2C
#define ST7789_RAMRD   		0x2E
#define ST7789_PTLAR   		0x30
#define ST7789_COLMOD 		0x3A
#define ST7789_MADCTL  		0x36
#define ST7789_PORCTRL 		0xB2
#define ST7789_GCTRL   		0xB7
#define ST7789_VCOMS   		0xBB
#define ST7789_LCMCTRL		0xC0
#define ST7789_VDVVRHEN 	0xC2
#define ST7789_VRHS			0xC3
#define ST7789_VDVS			0xC4
#define ST7789_FRCTRL2		0xC6
#define ST7789_PWCTRL1		0xD0
#define ST7789_RDID1   		0xDA
#define ST7789_RDID2   		0xDB
#define ST7789_RDID3   		0xDC
#define ST7789_RDID4   		0xDD
#define ST7789_PVGAMCTRL	0xE0
#define ST7789_NVGAMCTRL	0xE1

//параметры конфигурации памяти дисплея
#define ST7789_MADCTL_MY			0x80 //бит D7
#define ST7789_MADCTL_MX			0x40 //бит D6
#define ST7789_MADCTL_MV			0x20 //бит D5
#define ST7789_MADCTL_ML			0x10 //бит D4
#define ST7789_MADCTL_BGR			0x08 //бит D3
#define ST7789_MADCTL_MH			0x04 //бит D2

//режимы цвета
#define ST7789_COLOR_MODE_16bit 0x55    //RGB565 (16bit)
#define ST7789_COLOR_MODE_18bit 0x66    //RGB666 (18bit)

uint8_t* ST7789_Init(uint8_t orientation);
uint8_t* ST7789_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
uint8_t* ST7789_SleepIn(void);
uint8_t* ST7789_SleepOut(void);

#endif
