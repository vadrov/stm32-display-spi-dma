/*
 *  display.h
 *	Драйвер управления дисплеями по SPI
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

#ifndef INC_DISPLAY_H_
#define INC_DISPLAY_H_

#include "main.h"
#include "fonts.h"

//некоторые предопределенные цвета
//формат 0xRRGGBB
#define	COLOR_BLACK			0x000000
#define	COLOR_BLUE			0x0000FF
#define	COLOR_RED			0xFF0000
#define	COLOR_GREEN			0x00FF00
#define COLOR_CYAN			0x00FFFF
#define COLOR_MAGENTA		0xFF00FF
#define COLOR_YELLOW		0xFFFF00
#define COLOR_WHITE			0xFFFFFF
#define COLOR_NAVY			0x000080
#define COLOR_DARKGREEN		0x2F4F2F
#define COLOR_DARKCYAN		0x008B8B
#define COLOR_MAROON		0xB03060
#define COLOR_PURPLE		0x800080
#define COLOR_OLIVE			0x808000
#define COLOR_LIGHTGREY		0xD3D3D3
#define COLOR_DARKGREY		0xA9A9A9
#define COLOR_ORANGE		0xFFA500
#define COLOR_GREENYELLOW	0xADFF2F

#define LCD_UPR_COMMAND		0
#define LCD_UPR_DATA		1
#define LCD_UPR_PAUSE		2
#define LCD_UPR_END			3

//ширина данных
typedef enum {
	LCD_DATA_UNKNOW_BUS,
	LCD_DATA_8BIT_BUS,
	LCD_DATA_16BIT_BUS
} LCD_DATA_BUS;

//статусы дисплея
typedef enum {
	LCD_STATE_READY,
	LCD_STATE_BUSY,
	LCD_STATE_ERROR,
	LCD_STATE_UNKNOW
} LCD_State;

//ориентация дисплея
typedef enum {
	PAGE_ORIENTATION_PORTRAIT,			//портрет
	PAGE_ORIENTATION_LANDSCAPE,			//пейзаж
	PAGE_ORIENTATION_PORTRAIT_MIRROR,	//портрет перевернуто
	PAGE_ORIENTATION_LANDSCAPE_MIRROR	//пейзаж перевернуто
} LCD_PageOrientation;

//режимы печати символов
typedef enum {
	LCD_SYMBOL_PRINT_FAST,		//быстрый с затиранием фона
	LCD_SYMBOL_PRINT_PSETBYPSET	//медленный, по точкам, без затирания фона
} LCD_PrintSymbolMode;

//данные DMA
typedef struct {
	DMA_TypeDef *dma;
	uint32_t stream;
} LCD_DMA_TypeDef;

//данные spi подключения
typedef struct {
	SPI_TypeDef *spi;
	LCD_DMA_TypeDef dma_tx;
	GPIO_TypeDef *reset_port;
	uint16_t reset_pin;
	GPIO_TypeDef *dc_port;
	uint16_t dc_pin;
	GPIO_TypeDef *cs_port;
	uint16_t cs_pin;
} LCD_SPI_Connected_data;

//подсветка
typedef struct {
	TIM_TypeDef *htim_bk;		//------- для подсветки с PWM - таймер
	uint32_t channel_htim_bk;	//----------------------------- канал таймера

	GPIO_TypeDef *blk_port;		//просто для включения и выключения подсветки, если htim_bk = 0 (без PWM, определен порт вывода)
	uint16_t blk_pin;			//----------------------------------------------------------------------- пин порта

	uint8_t bk_percent;			//яркость подсветки для режима PWM, %
								//либо 0 - подсветка отключена, > 0 подсветка включена, если если htim_bk = 0 (без PWM, определен порт вывода)
} LCD_BackLight_data;

//коллбэки
typedef uint8_t* (*DisplayInitCallback)(uint8_t);
typedef uint8_t* (*DisplaySetWindowCallback)(uint16_t, uint16_t, uint16_t, uint16_t);
typedef uint8_t* (*DisplaySleepInCallback)(void);
typedef uint8_t* (*DisplaySleepOutCallback)(void);

//позиция печати
typedef struct {
	uint16_t x;
	uint16_t y;
} LCD_xy_pos;

//обработчик дисплея
typedef struct {
	uint16_t Width_Controller;    	//максимальная ширина матрицы, поддерживаемая контроллером дисплея, пиксели
	uint16_t Height_Controller;		//максимальная высота матрицы, поддерживаемая контроллером дисплея, пиксели
	uint16_t Width;					//фактическая ширина матрицы используемого дисплея, пиксели
	uint16_t Height;				//фактическая высота матрицы используемого дисплея, пиксели
	LCD_PageOrientation Orientation;//ориентация дисплея
	int16_t x_offs;					//смещение по x
	int16_t y_offs;					//смещение по y
	LCD_xy_pos AtPos;				//текущая позиция печати символа
	DisplayInitCallback Init_callback;					//коллбэк инициализации
	DisplaySetWindowCallback SetActiveWindow_callback;	//коллбэк установки окна вывода
	DisplaySleepInCallback SleepIn_callback;			//коллбэк "входа в сон"
	DisplaySleepOutCallback SleepOut_callback;			//коллбэк "выхода из сна"
	LCD_SPI_Connected_data spi_data;					//данные подключения по SPI
	LCD_DATA_BUS data_bus;								//ширина данных
	LCD_BackLight_data bkl_data;						//данные подсветки
	uint8_t DMA_TX_Complete;							//флаг окончания передачи
	uint32_t DMA_Counter;								//счетчик повторов DMA
	uint16_t *tmp_buf;									//указатель на буфер дисплея
	uint8_t display_number;								//номер дисплея
} LCD_Handler;


extern uint8_t LCD_TOTAL_DISPLAYS;
extern LCD_Handler **LCD_DISPLAY_HANDLERS;

//коллбэк (внести в обработчик прерывания потока DMA, выделенного для дисплея, см. файл stm32f4xx_it.c)
extern void Display_TC_Callback(DMA_TypeDef*, uint32_t);

//создание обработчика нового дисплея - возвращает указатель на обработчик дисплея
LCD_Handler* LCD_Create(	uint16_t resolution1,
							uint16_t resolution2,
							uint16_t width_controller,
							uint16_t height_controller,
							LCD_PageOrientation orientation,
							DisplayInitCallback init,
							DisplaySetWindowCallback set_win,
							DisplaySleepInCallback sleep_in,
							DisplaySleepOutCallback sleep_out,
							void *connection_data,
							LCD_DATA_BUS data_bus,
							LCD_BackLight_data bkl_data
					   );

//удаляет обработчик дисплея
void LCD_Delete(LCD_Handler* lcd);
//аппаратный сброс дисплея
void LCD_HardWareReset(LCD_Handler* lcd);
//инициализация дисплея
void LCD_Init(LCD_Handler* lcd);
//интерпретатор командных строк
void LCD_String_Interpretator(LCD_Handler* lcd, uint8_t *str);
//установка яркости дисплея
void LCD_SetBackLight(LCD_Handler* lcd, uint8_t bk_percent);
//возвращает текущую яркость дисплея
uint8_t LCD_GetBackLight(LCD_Handler* lcd);
//возвращает ширину дисплея, пиксели
uint16_t LCD_GetWidth(LCD_Handler* lcd);
//возвращает высоту дисплея, пиксели
uint16_t LCD_GetHeight(LCD_Handler* lcd);
//возвращает статус дисплея
LCD_State LCD_GetState(LCD_Handler* lcd);
//переводит дисплей в режим сна
void LCD_SleepIn(LCD_Handler* lcd);
//выводит дисплей из режима сна
void LCD_SleepOut(LCD_Handler* lcd);
//устанавливает окно вывода
void LCD_SetActiveWindow(LCD_Handler* lcd, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
//отправляет данные на дисплей - без DMA
void LCD_WriteData(LCD_Handler *lcd, uint16_t *data, uint32_t len);
//отправляет данные на дисплей с использованием DMA
void LCD_WriteDataDMA(LCD_Handler *lcd, uint16_t *data, uint32_t len);
//заливает окно с заданными координатами заданным цветом
void LCD_FillWindow(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);
//заливает весь экран заданным цветом
void LCD_Fill(LCD_Handler* lcd, uint32_t color);
//рисует точку в заданных координатах заданным цветом
void LCD_DrawPixel(LCD_Handler* lcd, uint16_t x, uint16_t y, uint32_t color);
//рисует линию по заданным координатам заданным цветом
void LCD_DrawLine(LCD_Handler* lcd, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color);
//рисует прямоугольник по заданным координатам заданным цветом
void LCD_DrawRectangle(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);
//рисует закрашенный прямоугольник по заданным координатам заданным цветом
void LCD_DrawFilledRectangle(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);
//рисует окружность с заданным центром и радиусом с заданным цветом
void LCD_DrawCircle(LCD_Handler* lcd, uint16_t x0, uint16_t y0, uint8_t r, uint32_t color);
//пересылает на дисплей блок памяти (например, кусок изображения)
void LCD_DrawImage(LCD_Handler* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data, uint8_t dma_use_flag);
//выводит символ в указанной позиции
void LCD_WriteChar(LCD_Handler* lcd, uint16_t x, uint16_t y, char ch, FontDef *font, uint32_t txcolor, uint32_t bgcolor, LCD_PrintSymbolMode modesym);
//выцводит строку символов с указанной позиции
void LCD_WriteString(LCD_Handler* lcd, uint16_t x, uint16_t y, const char *str, FontDef *font, uint32_t color, uint32_t bgcolor, LCD_PrintSymbolMode modesym);
//рисует треугольник
void LCD_DrawTriangle(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint32_t color);
//рисует закрашенный прямоугольник
void LCD_DrawFilledTriangle(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint32_t color);
//рисует закрашенную окружность
void LCD_DrawFilledCircle(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t r, uint32_t color);
//преобразует цвет в формате R8G8B8 (24 бита) в 16 битовый R5G6B5
uint16_t LCD_Color(LCD_Handler *lcd, uint8_t r, uint8_t g, uint8_t b);
uint16_t LCD_Color_24b_to_16b(LCD_Handler *lcd, uint32_t color);

#endif /* INC_DISPLAY_H_ */
