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

#ifndef __FONT_H
#define __FONT_H

#include "main.h"

typedef struct {
    const uint8_t width;
    const uint8_t height;
    const uint8_t *data;
    const uint8_t firstcode;
    const uint8_t lastcode;
} FontDef;

typedef struct {
    const uint16_t width;
    const uint16_t height;
    const uint16_t *data;
} tImage;

extern tImage image_cover;

extern FontDef Font_8x13;
extern FontDef Font_15x25;
extern FontDef Font_12x20;
#endif
