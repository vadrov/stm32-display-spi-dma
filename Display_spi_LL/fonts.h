/*
*  fonts.h 
*  Драйвер управления дисплеями по SPI
*  Created on: 19 сент. 2020 г.
*  Author: VadRov
*  Версия: 1.1 LL (на регистрах и частично LL)
*  для STM32F4
*
*  https://www.youtube.com/c/VadRov
*  https://zen.yandex.ru/vadrov
*  https://vk.com/vadrov
*  https://t.me/vadrov_channel
*/

#ifndef __FONT_H
#define __FONT_H

#include "stdint.h"

typedef struct {
    const uint8_t width;
    const uint8_t height;
    const void *data;
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
