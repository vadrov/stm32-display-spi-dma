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
 *
 */

#include "display.h"
#include "fonts.h"
#include <string.h>
#ifdef LCD_DYNAMIC_MEM
#include "stdlib.h"
#endif

#define ABS(x) ((x) > 0 ? (x) : -(x))

#define min(x1,x2)	(x1 < x2 ? x1 : x2)
#define max(x1,x2)	(x1 > x2 ? x1 : x2)

#define min3(x1,x2,x3)	min(min(x1,x2),x3)
#define max3(x1,x2,x3)	max(max(x1,x2),x3)

#define LCD_CS_LOW		if (lcd->spi_data.cs_port) { lcd->spi_data.cs_port->BSRR = (uint32_t)lcd->spi_data.cs_pin << 16U; }
#define LCD_CS_HI		if (lcd->spi_data.cs_port) { lcd->spi_data.cs_port->BSRR = lcd->spi_data.cs_pin; }
#define LCD_DC_LOW		{lcd->spi_data.dc_port->BSRR = (uint32_t)lcd->spi_data.dc_pin << 16U; }
#define LCD_DC_HI		{lcd->spi_data.dc_port->BSRR = lcd->spi_data.dc_pin; }

#define DISABLE_IRQ		__disable_irq(); __DSB(); __ISB();
#define ENABLE_IRQ		__enable_irq();

LCD_Handler *LCD = 0; //список дисплеев

//коллбэк по прерыванию потока передачи
//этот обработчик необходимо прописать в функциях обработки прерываний в потоках DMA,
//которые используются дисплеями - stm32f4xx_it.c
void Display_TC_Callback(DMA_TypeDef *dma_x, uint32_t stream)
{
	//сбрасываем флаги прерываний
	uint8_t shift[8] = {0, 6, 16, 22, 0, 6, 16, 22}; //битовое смещение во флаговом регистре IFCR (L и H)
	volatile uint32_t *ifcr_tx = (stream > 3) ? &(dma_x->HIFCR) : &(dma_x->LIFCR);
	*ifcr_tx = 0x3F<<shift[stream];
	uint32_t stream_ct = 0;
	DMA_TypeDef *dma_ct = 0;
	LCD_Handler *lcd = LCD; //указатель на первый дисплей в списке
	//проходим по списку дисплеев (пока есть следующий в списке)
	while (lcd) {
		//получаем параметры DMA потока дисплея
		dma_ct = lcd->spi_data.dma_tx.dma;
		stream_ct = lcd->spi_data.dma_tx.stream;
		//проверка на соответствие текущего потока DMA потоку, к которому привязан i-тый дисплей
		if (dma_ct == dma_x && stream_ct == stream) {
			if (lcd->spi_data.cs_port) {//управление по cs поддерживается?
				//на выводе cs дисплея низкий уровень?
				if (lcd->spi_data.cs_port->ODR & lcd->spi_data.cs_pin) { //проверяем состояние пина выходного регистра порта
					lcd = (LCD_Handler *)lcd->next;		   //если высокий уровень cs, то не этот дисплей активен
					continue;							   //и переходим к следующему
				}
			}
			//указатель на поток: aдрес контроллера + смещение
			DMA_Stream_TypeDef *dma_TX = ((DMA_Stream_TypeDef *)((uint32_t)((uint32_t)dma_x + STREAM_OFFSET_TAB[stream])));
			//выключаем поток DMA
			dma_TX->CR &= ~DMA_SxCR_EN;
			while (dma_TX->CR & DMA_SxCR_EN) ; //ждем отключения потока
			if (lcd->size_mem) { //если переданы не все данные из памяти, то перезапускаем DMA и выходим из прерывания
				if (lcd->size_mem > 65535) {
					dma_TX->NDTR = 65535;
					lcd->size_mem -= 65535;
				}
				else {
					dma_TX->NDTR = lcd->size_mem;
					lcd->size_mem = 0;
				}
				//включаем поток DMA
				dma_TX->CR |= (DMA_SxCR_EN);
				return;
			}
#ifdef LCD_DYNAMIC_MEM
			//очищаем буфер дисплея
			if (lcd->tmp_buf) {
		    	//так как память выделяется динамически, то на всякий случай,
				//чтобы не было коллизий, запретим прерывания перед освобождением памяти
				DISABLE_IRQ
				free(lcd->tmp_buf);
				lcd->tmp_buf = 0;
				ENABLE_IRQ
			}
#endif
			//запрещаем SPI отправлять запросы к DMA
			lcd->spi_data.spi->CR2 &= ~SPI_CR2_TXDMAEN;
			while (lcd->spi_data.spi->SR & SPI_SR_BSY) ; //ждем пока SPI освободится
			//отключаем дисплей от MK (притягиваем вывод CS дисплея к высокому уровню)
			if (!lcd->cs_control) { LCD_CS_HI }
			//выключаем spi
			lcd->spi_data.spi->CR1 &= ~SPI_CR1_SPE;
			return;
		}
		//переходим к следующему дисплею в списке
		lcd = (LCD_Handler *)lcd->next;
	}
}

inline void LCD_SetCS(LCD_Handler *lcd)
{
	LCD_CS_HI
}

inline void LCD_ResCS(LCD_Handler *lcd)
{
	LCD_CS_LOW
}

inline void LCD_SetDC(LCD_Handler *lcd)
{
	LCD_DC_HI
}

inline void LCD_ResDC(LCD_Handler *lcd)
{
	LCD_DC_LOW
}

typedef enum {
	lcd_write_command = 0,
	lcd_write_data
} lcd_dc_select;

inline static void LCD_WRITE_DC(LCD_Handler* lcd, uint8_t data, lcd_dc_select lcd_dc)
{
	SPI_TypeDef *spi = lcd->spi_data.spi;
	if (lcd_dc == lcd_write_command)  {
		LCD_DC_LOW
	}
	else {
		LCD_DC_HI
	}
	LL_SPI_TransmitData8(spi, data);
	while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
	while (spi->SR & SPI_SR_BSY)    ; //ждем когда SPI освободится
}

void LCD_HardWareReset (LCD_Handler* lcd)
{
	if (lcd->spi_data.reset_port) {
		lcd->spi_data.reset_port->BSRR = (uint32_t)lcd->spi_data.reset_pin << 16U;
		LL_mDelay(25);
		lcd->spi_data.reset_port->BSRR = lcd->spi_data.reset_pin;
		LL_mDelay(25);
	}
}

//интерпретатор строк с управлящими кодами: "команда", "данные", "пауза", "завершение пакета"
void LCD_String_Interpretator(LCD_Handler* lcd, uint8_t *str)
{
	SPI_TypeDef *spi = lcd->spi_data.spi;
	int i;
	while (LCD_GetState(lcd) == LCD_STATE_BUSY) ; //ждем когда дисплей освободится
	if (!lcd->cs_control) { LCD_CS_LOW }
	spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
				   SPI_CR1_RXONLY |   	//  Transmit only
				   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
				   SPI_CR1_DFF); 		//установим 8-битную передачу
	spi->CR1 |= SPI_CR1_SPE; // SPI включаем
	while (1) {
		switch (*str++) {
			//управляющий код "команда"
			case LCD_UPR_COMMAND:
				//отправляем код команды контроллеру дисплея
				LCD_WRITE_DC(lcd, *str++, lcd_write_command);
				//количество параметров команды
				i = *str++;
				//отправляем контроллеру дисплея параметры команды
				while(i--) {
					LCD_WRITE_DC(lcd, *str++, lcd_write_data);
				}
				break;
			//управляющий код "данные"
			case LCD_UPR_DATA:
				//количество данных
				i = *str++;
				//отправляем контроллеру дисплея данные
				while(i--) {
					LCD_WRITE_DC(lcd, *str++, lcd_write_data);
				}
				break;
			//управляющий код "пауза"
			case LCD_UPR_PAUSE:
				//ожидание в соответствии с параметром (0...255)
				LL_mDelay(*str++);
				break;
			//управляющий код "завершение пакета"
			case LCD_UPR_END:
			default:
				if (!lcd->cs_control) { LCD_CS_HI }
				//выключаем spi
				spi->CR1 &= ~SPI_CR1_SPE;
				return;
		}
	}
}

//создание обработчика дисплея и добавление его в список дисплеев lcds
//возвращает указатель на созданный обработчик либо 0 при неудаче
LCD_Handler* LCD_DisplayAdd(LCD_Handler *lcds,     /* указатель на список дисплеев
													  (первый дисплей в списке) */
#ifndef LCD_DYNAMIC_MEM
							LCD_Handler *lcd,	   /* указатель на создаваемый обработчик дисплея
													  в случае статического выделения памяти */
#endif
							uint16_t resolution1,
							uint16_t resolution2,
							uint16_t width_controller,
							uint16_t height_controller,
							int16_t w_offs,
							int16_t h_offs,
							LCD_PageOrientation orientation,
							DisplayInitCallback init,
							DisplaySetWindowCallback set_win,
							DisplaySleepInCallback sleep_in,
							DisplaySleepOutCallback sleep_out,
							void *connection_data,
							LCD_DATA_BUS data_bus,
							LCD_BackLight_data bkl_data
					   )
{
#ifdef LCD_DYNAMIC_MEM
	LCD_Handler* lcd = (LCD_Handler*)malloc(sizeof(LCD_Handler));
#endif
	if (!lcd) return 0;
	memset(lcd, 0, sizeof(LCD_Handler));
	LCD_DMA_TypeDef *hdma = 0;
	lcd->data_bus = data_bus;
	//инициализация данных подключения
	lcd->spi_data = *((LCD_SPI_Connected_data*)connection_data);
	hdma = &lcd->spi_data.dma_tx;
	//настройка DMA
	if (hdma->dma) {
		DMA_Stream_TypeDef *dma_x = ((DMA_Stream_TypeDef *)((uint32_t)((uint32_t)hdma->dma + STREAM_OFFSET_TAB[hdma->stream])));
		dma_x->CR &= ~DMA_SxCR_EN; //отключаем канал DMA
		while(dma_x->CR & DMA_SxCR_EN) ; //ждем отключения канала
		if (lcd->data_bus == LCD_DATA_8BIT_BUS) {
			dma_x->CR &= ~(DMA_SxCR_MSIZE | DMA_SxCR_PSIZE);
			dma_x->CR |= LL_DMA_MDATAALIGN_BYTE | LL_DMA_PDATAALIGN_BYTE;
		}
		else if (lcd->data_bus == LCD_DATA_16BIT_BUS) {
			dma_x->CR &= ~(DMA_SxCR_MSIZE | DMA_SxCR_PSIZE);
			dma_x->CR |= LL_DMA_MDATAALIGN_HALFWORD | LL_DMA_PDATAALIGN_HALFWORD;
		}
		//запрещаем прерывания по некоторым событиям канала передачи tx и режим двойного буфера
		dma_x->CR &= ~(DMA_SxCR_DMEIE | DMA_SxCR_HTIE | DMA_SxCR_DBM | DMA_SxCR_TEIE);
		dma_x->FCR &= ~DMA_SxFCR_FEIE;
		//разрешаем прерывание по окончанию передачи
		dma_x->CR |= DMA_SxCR_TCIE;
		dma_x->CR &= ~DMA_SxCR_PINC; //инкремент адреса периферии отключен
		dma_x->CR |= DMA_SxCR_MINC;  //инкремент адреса памяти включен
	}
	//настройка ориентации дисплея и смещения начала координат
	uint16_t max_res = max(resolution1, resolution2);
	uint16_t min_res = min(resolution1, resolution2);
	if (orientation==PAGE_ORIENTATION_PORTRAIT || orientation==PAGE_ORIENTATION_PORTRAIT_MIRROR) {
		lcd->Width = min_res;
		lcd->Height = max_res;
		lcd->Width_Controller = width_controller;
		lcd->Height_Controller = height_controller;
		if (orientation==PAGE_ORIENTATION_PORTRAIT) {
			lcd->x_offs = w_offs;
			lcd->y_offs = h_offs;
		}
		else {
			lcd->x_offs = lcd->Width_Controller - lcd->Width - w_offs;
			lcd->y_offs = lcd->Height_Controller - lcd->Height - h_offs;
		}
	}
	else if (orientation==PAGE_ORIENTATION_LANDSCAPE || orientation==PAGE_ORIENTATION_LANDSCAPE_MIRROR)	{
		lcd->Width = max_res;
		lcd->Height = min_res;
		lcd->Width_Controller = height_controller;
		lcd->Height_Controller = width_controller;
		if (orientation==PAGE_ORIENTATION_LANDSCAPE) {
			lcd->x_offs = h_offs;
			lcd->y_offs = lcd->Height_Controller - lcd->Height - w_offs;
		}
		else {
			lcd->x_offs = lcd->Width_Controller - lcd->Width - h_offs;
			lcd->y_offs = w_offs;
		}
	}
	else {
		LCD_Delete(lcd);
		return 0;
	}

	if (lcd->Width_Controller < lcd->Width ||
		lcd->Height_Controller < lcd->Height ||
		init==NULL ||
		set_win==NULL )	{
		LCD_Delete(lcd);
		return 0;
	}
	lcd->Orientation = orientation;
	lcd->Init_callback = init;
	lcd->SetActiveWindow_callback = set_win;
	lcd->SleepIn_callback = sleep_in;
	lcd->SleepOut_callback = sleep_out;
	lcd->bkl_data = bkl_data;
	lcd->display_number = 0;
	lcd->next = 0;
	lcd->prev = 0;
	lcd->tmp_buf = 0;
	if (!lcds) {
		return lcd;
	}
	LCD_Handler *prev = lcds;
	while (prev->next) {
		prev = (LCD_Handler *)prev->next;
		lcd->display_number++;
	}
	lcd->prev = (void*)prev;
	prev->next = (void*)lcd;
	return lcd;
}

//удаляет дисплей
void LCD_Delete(LCD_Handler* lcd)
{
	if (lcd) {
#ifdef LCD_DYNAMIC_MEM
		if (lcd->tmp_buf) {
			free(lcd->tmp_buf);
		}
#endif
		memset(lcd, 0, sizeof(LCD_Handler));
#ifdef LCD_DYNAMIC_MEM
		free(lcd);
#endif
	}
}

//инициализирует дисплей
void LCD_Init(LCD_Handler* lcd)
{
	LCD_HardWareReset(lcd);
	LCD_String_Interpretator(lcd, lcd->Init_callback(lcd->Orientation));
	LCD_SetBackLight(lcd, lcd->bkl_data.bk_percent);
}

//возвращает яркость подсветки, %
inline uint8_t LCD_GetBackLight(LCD_Handler* lcd)
{
	return lcd->bkl_data.bk_percent;
}

//возвращает ширину дисплея, пиксели
inline uint16_t LCD_GetWidth(LCD_Handler* lcd)
{
	return lcd->Width;
}

//возвращает высоту дисплея, пиксели
inline uint16_t LCD_GetHeight(LCD_Handler* lcd)
{
	return lcd->Height;
}

//возвращает статус дисплея: занят либо свободен (требуется для отправки новых данных на дисплей)
//дисплей занят, если занято spi, к которому он подключен
inline LCD_State LCD_GetState(LCD_Handler* lcd)
{
	if (lcd->spi_data.spi->CR1 & SPI_CR1_SPE) {
		return LCD_STATE_BUSY;
	}
	return LCD_STATE_READY;
}

//управление подсветкой
void LCD_SetBackLight(LCD_Handler* lcd, uint8_t bk_percent)
{
	if (bk_percent > 100) {
		bk_percent = 100;
	}
	lcd->bkl_data.bk_percent = bk_percent;
	//подсветка с использованием PWM
	if (lcd->bkl_data.htim_bk) {
		//вычисляем % яркости, как часть от периода счетчика
		uint32_t bk_value = lcd->bkl_data.htim_bk->ARR * bk_percent / 100;
		//задаем скважность PWM конкретного канала
		switch(lcd->bkl_data.channel_htim_bk) {
			case LL_TIM_CHANNEL_CH1:
				lcd->bkl_data.htim_bk->CCR1 = bk_value;
				break;
			case LL_TIM_CHANNEL_CH2:
				lcd->bkl_data.htim_bk->CCR2 = bk_value;
				break;
			case LL_TIM_CHANNEL_CH3:
				lcd->bkl_data.htim_bk->CCR3 = bk_value;
				break;
			case LL_TIM_CHANNEL_CH4:
				lcd->bkl_data.htim_bk->CCR4 = bk_value;
				break;
			default:
				break;
		}
		//если таймер не запущен, то запускаем его
		if (!(lcd->bkl_data.htim_bk->CR1 & TIM_CR1_CEN)) {
			//включаем канал
			lcd->bkl_data.htim_bk->CCER |= lcd->bkl_data.channel_htim_bk;
			//включаем счетчик
			lcd->bkl_data.htim_bk->CR1 |= TIM_CR1_CEN;
		}
	}
	//подсветка без PWM (просто вкл./выкл.), если таймер с PWM недоступен
	else if (lcd->bkl_data.blk_port) {
		if (bk_percent) {
			lcd->bkl_data.blk_port->BSRR = lcd->bkl_data.blk_pin;
		}
		else {
			lcd->bkl_data.blk_port->BSRR = (uint32_t)lcd->bkl_data.blk_pin << 16U;
		}
	}
}

//перевод дисплея в "спящий режим" (выключение отображения дисплея и подсветки)
void LCD_SleepIn(LCD_Handler* lcd)
{
	//подсветка с использованием PWM
	if (lcd->bkl_data.htim_bk) {
		//выключаем подсветку, установив нулевую скважность
		switch(lcd->bkl_data.channel_htim_bk) {
			case LL_TIM_CHANNEL_CH1:
				lcd->bkl_data.htim_bk->CCR1 = 0;
				break;
			case LL_TIM_CHANNEL_CH2:
				lcd->bkl_data.htim_bk->CCR2 = 0;
				break;
			case LL_TIM_CHANNEL_CH3:
				lcd->bkl_data.htim_bk->CCR3 = 0;
				break;
			case LL_TIM_CHANNEL_CH4:
				lcd->bkl_data.htim_bk->CCR4 = 0;
				break;
			default:
				break;
		}
	}
	//подсветка без PWM (просто вкл./выкл.), если таймер с PWM недоступен
	else if (lcd->bkl_data.blk_port) {
		lcd->bkl_data.blk_port->BSRR = (uint32_t)lcd->bkl_data.blk_pin << 16U;
	}
	if (lcd->SleepIn_callback) {
		LCD_String_Interpretator(lcd, lcd->SleepIn_callback());
	}
}

//вывод дисплея из "спящего режима" (включение отображения дисплея и подсветки)
void LCD_SleepOut(LCD_Handler* lcd)
{
	if (lcd->SleepOut_callback) {
		LCD_String_Interpretator(lcd, lcd->SleepOut_callback());
	}
	//включение подсветки
	LCD_SetBackLight(lcd, lcd->bkl_data.bk_percent);
}

//установка на дисплее окна вывода
void LCD_SetActiveWindow(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	LCD_String_Interpretator(lcd, lcd->SetActiveWindow_callback(x1 + lcd->x_offs, y1 + lcd->y_offs, x2 + lcd->x_offs, y2 + lcd->y_offs));
}

//вывод блока данных на дисплей
void LCD_WriteData(LCD_Handler *lcd, uint16_t *data, uint32_t len)
{
	SPI_TypeDef *spi = lcd->spi_data.spi;
	while (LCD_GetState(lcd) == LCD_STATE_BUSY) ; //ждем когда дисплей освободится
	if (!lcd->cs_control) { LCD_CS_LOW }
	if (!lcd->dc_control) { LCD_DC_HI  }
	spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
				   SPI_CR1_RXONLY |   	//  Transmit only
				   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
				   SPI_CR1_DFF); 		//8-битная передача
	if (lcd->data_bus == LCD_DATA_16BIT_BUS) {
		spi->CR1 |= SPI_CR1_DFF; //16-битная передача
	}
	spi->CR1 |= SPI_CR1_SPE; //SPI включаем
	if (lcd->data_bus == LCD_DATA_16BIT_BUS) {
		while (len--) {
			LL_SPI_TransmitData16(spi, *data++); //записываем данные в регистр
			while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
		}
	}
	else {
		len *= 2;
		uint8_t *data1 = (uint8_t*)data;
		while (len--)	{
			LL_SPI_TransmitData8(spi, *data1++); //записываем данные в регистр
			while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
		}
	}
	while (spi->SR & SPI_SR_BSY) ; //ждем когда SPI освободится
	if (!lcd->cs_control) { LCD_CS_HI }
	//выключаем spi
	spi->CR1 &= ~SPI_CR1_SPE;
}

//вывод блока данных на дисплей с DMA
void LCD_WriteDataDMA(LCD_Handler *lcd, uint16_t *data, uint32_t len)
{
	if (lcd->spi_data.dma_tx.dma) {
		if (lcd->data_bus == LCD_DATA_8BIT_BUS) {
			len *= 2;
		}
		SPI_TypeDef *spi = lcd->spi_data.spi;
		while (LCD_GetState(lcd) == LCD_STATE_BUSY) ; //ждем когда дисплей освободится
		if (!lcd->cs_control) { LCD_CS_LOW }
		if (!lcd->dc_control) { LCD_DC_HI  }
		lcd->size_mem = len;
		spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
					   SPI_CR1_RXONLY |   	//  Transmit - передача
					   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
					   SPI_CR1_DFF); 		//8-битная передача
		if (lcd->data_bus == LCD_DATA_16BIT_BUS) {
			spi->CR1 |= SPI_CR1_DFF; //16-битная передача
		}
		spi->CR1 |= SPI_CR1_SPE; //SPI включаем
		DMA_TypeDef *dma_x = lcd->spi_data.dma_tx.dma;
		uint32_t stream = lcd->spi_data.dma_tx.stream;
		DMA_Stream_TypeDef *dma_TX = ((DMA_Stream_TypeDef *)((uint32_t)((uint32_t)dma_x + STREAM_OFFSET_TAB[stream])));
		uint8_t shift[8] = {0, 6, 16, 22, 0, 6, 16, 22}; //битовое смещение во флаговых регистрах IFCR (L и H)
		volatile uint32_t *ifcr_tx = (stream > 3) ? &(dma_x->HIFCR) : &(dma_x->LIFCR);
		//сбрасываем флаги прерываний tx
		*ifcr_tx = 0x3F<<shift[stream];
		//разрешаем spi принимать запросы от DMA
		spi->CR2 |= SPI_CR2_TXDMAEN;
		//настраиваем адреса, длину, инкременты
		dma_TX->PAR = (uint32_t)(&spi->DR); //приемник периферия - адрес регистра DR spi
		dma_TX->M0AR = (uint32_t)data; //источник память - адрес буфера исходящих данных
		dma_TX->CR &= ~DMA_SxCR_PINC; //инкремент адреса периферии отключен
		dma_TX->CR |= DMA_SxCR_MINC;  //инкремент адреса памяти включен
		if (len <= 65535) {
			dma_TX->NDTR = (uint32_t)len; //размер передаваемых данных
			lcd->size_mem = 0;
		}
		else {
			dma_TX->NDTR = 65535;
			lcd->size_mem = len - 65535;
		}
		dma_TX->CR |= (DMA_SxCR_EN); //включение канала передачи (старт DMA передачи)
		return;
	}
	LCD_WriteData(lcd, data, len);
}

void LCD_FillWindow(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
	uint16_t tmp;
	if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
	if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }
	if (x1 > lcd->Width - 1 || y1 > lcd->Height - 1) return;
	if (x2 > lcd->Width - 1)  x2 = lcd->Width - 1;
	if (y2 > lcd->Height - 1) y2 = lcd->Height - 1;
	uint32_t len = (x2 - x1 + 1) * (y2 - y1 + 1); //количество закрашиваемых пикселей
	LCD_SetActiveWindow(lcd, x1, y1, x2, y2);
	if (!lcd->cs_control) LCD_CS_LOW
	if (!lcd->dc_control) LCD_DC_HI
	uint16_t color16 = lcd->fill_color = LCD_Color_24b_to_16b(lcd, color);
	SPI_TypeDef *spi = lcd->spi_data.spi;
	spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
				   SPI_CR1_RXONLY |   	//  Transmit only
				   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
				   SPI_CR1_DFF); 		//8-битная передача
	if (lcd->data_bus == LCD_DATA_16BIT_BUS) {
		spi->CR1 |= SPI_CR1_DFF; //16-битная передача
	}
	spi->CR1 |= SPI_CR1_SPE; // SPI включаем
	if (lcd->spi_data.dma_tx.dma)
	{
		if (lcd->data_bus == LCD_DATA_8BIT_BUS)	{
			len *= 2;
		}
		DMA_TypeDef *dma_x = lcd->spi_data.dma_tx.dma;
		uint32_t stream = lcd->spi_data.dma_tx.stream;
		DMA_Stream_TypeDef *dma_TX = ((DMA_Stream_TypeDef *)((uint32_t)((uint32_t)dma_x + STREAM_OFFSET_TAB[stream])));
		uint8_t shift[8] = {0, 6, 16, 22, 0, 6, 16, 22}; //битовое смещение во флаговых регистрах IFCR (L и H)
		volatile uint32_t *ifcr_tx = (stream > 3) ? &(dma_x->HIFCR) : &(dma_x->LIFCR);
		//сбрасываем флаги прерываний tx
		*ifcr_tx = 0x3F<<shift[stream];
		//разрешаем spi отправлять запросы к DMA
		spi->CR2 |= SPI_CR2_TXDMAEN;
		//настраиваем адреса, длину, инкременты
		dma_TX->PAR = (uint32_t)(&spi->DR); //приемник периферия - адрес регистра DR spi
		dma_TX->M0AR = (uint32_t)&lcd->fill_color; //источник память - адрес буфера исходящих данных
		dma_TX->CR &= ~DMA_SxCR_PINC; //инкремент адреса периферии отключен
		dma_TX->CR &= ~DMA_SxCR_MINC; //инкремент адреса памяти отключен
		if (len <= 65535) {
			dma_TX->NDTR = (uint32_t)len; //размер передаваемых данных
			lcd->size_mem = 0;
		}
		else {
			dma_TX->NDTR = 65535;
			lcd->size_mem = len - 65535;
		}
		dma_TX->CR |= (DMA_SxCR_EN); //включение канала передачи (старт DMA передачи)
		return;
	}
	if (lcd->data_bus == LCD_DATA_16BIT_BUS) {
		while(len--) {
			LL_SPI_TransmitData16(spi, color16); //записываем данные в регистр
			while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
		}
	}
	else {
		uint8_t color1 = color16 & 0xFF, color2 = color16 >> 8;
		while(len--) {
			LL_SPI_TransmitData8(spi, color1);
			while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
			LL_SPI_TransmitData8(spi, color2);
			while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
			len--;
		}
	}
	while (spi->SR & SPI_SR_BSY) ; //ждем когда SPI освободится
	if (!lcd->cs_control) LCD_CS_HI
	//выключаем spi
	spi->CR1 &= ~SPI_CR1_SPE;
}

/* Закрашивает весь дисплей заданным цветом */
void LCD_Fill(LCD_Handler* lcd, uint32_t color)
{
	LCD_FillWindow(lcd, 0, 0, lcd->Width - 1, lcd->Height - 1, color);
}

/* Рисует точку в заданных координатах */
void LCD_DrawPixel(LCD_Handler* lcd, int16_t x, int16_t y, uint32_t color)
{
	if (x > lcd->Width - 1 || y > lcd->Height - 1 || x < 0 || y < 0)	return;
	LCD_SetActiveWindow(lcd, x, y, x, y);
	uint16_t color1 = LCD_Color_24b_to_16b(lcd, color);
	LCD_WriteData(lcd, &color1, 1);
}

/*
 * Рисует линию по координатам двух точек
 * Горизонтальные и вертикальные линии рисуются очень быстро
 */
void LCD_DrawLine(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint32_t color)
{
	if(x0 == x1 || y0 == y1) {
		int16_t tmp;
		if (x0 > x1) { tmp = x0; x0 = x1; x1 = tmp; }
		if (y0 > y1) { tmp = y0; y0 = y1; y1 = tmp; }
		if (x1 < 0 || x0 > lcd->Width - 1)  return;
		if (y1 < 0 || y0 > lcd->Height - 1) return;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		LCD_FillWindow(lcd, x0, y0, x1, y1, color);
		return;
	}
	int16_t swap;
    uint16_t steep = ABS(y1 - y0) > ABS(x1 - x0);
    if (steep) {
		swap = x0;
		x0 = y0;
		y0 = swap;

		swap = x1;
		x1 = y1;
		y1 = swap;
    }

    if (x0 > x1) {
		swap = x0;
		x0 = x1;
		x1 = swap;

		swap = y0;
		y0 = y1;
		y1 = swap;
    }

    int16_t dx, dy;
    dx = x1 - x0;
    dy = ABS(y1 - y0);

    int16_t err = dx / 2;
    int16_t ystep;

    if (y0 < y1) {
        ystep = 1;
    } else {
        ystep = -1;
    }

    for (; x0<=x1; x0++) {
        if (steep) {
            LCD_DrawPixel(lcd, y0, x0, color);
        } else {
            LCD_DrawPixel(lcd, x0, y0, color);
        }
        err -= dy;
        if (err < 0) {
            y0 += ystep;
            err += dx;
        }
    }
}

/* Рисует прямоугольник по координатам левого верхнего и правого нижнего углов */
void LCD_DrawRectangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t color)
{
	LCD_DrawLine(lcd, x1, y1, x2, y1, color);
	LCD_DrawLine(lcd, x1, y1, x1, y2, color);
	LCD_DrawLine(lcd, x1, y2, x2, y2, color);
	LCD_DrawLine(lcd, x2, y1, x2, y2, color);
}

/* Рисует закрашенный прямоугольник по координатам левого верхнего и правого нижнего углов */
void LCD_DrawFilledRectangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t color)
{
	int16_t tmp;
	if (x1 > x2) { tmp = x1; x1 = x2; x2 = tmp; }
	if (y1 > y2) { tmp = y1; y1 = y2; y2 = tmp; }
	if (x2 < 0 || x1 > lcd->Width - 1)  return;
	if (y2 < 0 || y1 > lcd->Height - 1) return;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	LCD_FillWindow(lcd, x1, y1, x2, y2, color);
}

/* Рисует треугольник по координатам трех точек */
void LCD_DrawTriangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint32_t color)
{
	LCD_DrawLine(lcd, x1, y1, x2, y2, color);
	LCD_DrawLine(lcd, x2, y2, x3, y3, color);
	LCD_DrawLine(lcd, x3, y3, x1, y1, color);
}

/* Виды пересечений отрезков */
typedef enum {
	LINES_NO_INTERSECT = 0, //не пересекаются
	LINES_INTERSECT,		//пересекаются
	LINES_MATCH				//совпадают (накладываются)
} INTERSECTION_TYPES;

/*
 * Определение вида пересечения и координат (по оси х) пересечения отрезка с координатами (x1,y1)-(x2,y2)
 * с горизонтальной прямой y = y0
 * Возвращает один из видов пересечения типа INTERSECTION_TYPES, а в переменных x_min, x_max - координату
 * либо диапазон пересечения (если накладываются).
 * В match инкрементирует количество накладываний (считаем результаты со всех нужных вызовов)
 */
static INTERSECTION_TYPES LinesIntersection(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t y0, int16_t *x_min, int16_t *x_max, uint8_t *match)
{
	if (y1 == y2) { //Частный случай - отрезок параллелен оси х
		if (y0 == y1) { //Проверка на совпадение
			*x_min = min(x1, x2);
			*x_max = max(x1, x2);
			(*match)++;
			return LINES_MATCH;
		}
		return LINES_NO_INTERSECT;
	}
	if (x1 == x2) { //Частный случай - отрезок параллелен оси y
		if (min(y1, y2) <= y0 && y0 <= max(y1, y2)) {
			*x_min = *x_max = x1;
			return LINES_INTERSECT;
		}
		return LINES_NO_INTERSECT;
	}
	//Определяем точку пересечения прямых (уравнение прямой получаем из координат точек, задающих отрезок)
	*x_min = *x_max = (x2 - x1) * (y0 - y1) / (y2 - y1) + x1;
	if (min(x1, x2) <= *x_min && *x_min <= max(x1, x2)) { //Если координата x точки пересечения принадлежит отрезку,
		return LINES_INTERSECT;							  //то есть пересечение
	}
	return LINES_NO_INTERSECT;
}

/* Рисует закрашенный треугольник по координатам трех точек */
void LCD_DrawFilledTriangle(LCD_Handler* lcd, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3, uint32_t color)
{
	//Сортируем координаты в порядке возрастания y
	int16_t tmp;
	if (y1 > y2) {
		tmp = y1; y1 = y2; y2 = tmp;
		tmp = x1; x1 = x2; x2 = tmp;
	}
	if (y1 > y3) {
		tmp = y1; y1 = y3; y3 = tmp;
		tmp = x1; x1 = x3; x3 = tmp;
	}
	if (y2 > y3) {
		tmp = y2; y2 = y3; y3 = tmp;
		tmp = x2; x2 = x3; x3 = tmp;
	}
	//Проверяем, попадает ли треугольник в область вывода
	if (y1 > lcd->Height - 1 ||	y3 < 0) return;
	int16_t xmin = min3(x1, x2, x3);
	int16_t xmax = max3(x1, x2, x3);
	if (xmax < 0 || xmin > lcd->Width - 1) return;
	uint8_t c_mas, match;
	int16_t x_mas[8], x_min, x_max;
	//"Обрезаем" координаты, выходящие за рабочую область дисплея
	int16_t y_start = y1 < 0 ? 0: y1;
	int16_t y_end = y3 > lcd->Height - 1 ? lcd->Height - 1: y3;
	//Проходим в цикле по точкам диапазона координаты y и ищем пересечение отрезка y = y[i] (где y[i]=y1...y3, 1)
	//со сторонами треугольника
	for (int16_t y = y_start; y < y_end; y++) {
		c_mas = match = 0;
		if (LinesIntersection(x1, y1, x2, y2, y, &x_mas[c_mas], &x_mas[c_mas + 1], &match)) {
			c_mas += 2;
		}
		if (LinesIntersection(x2, y2, x3, y3, y, &x_mas[c_mas], &x_mas[c_mas + 1], &match)) {
			c_mas += 2;
		}
		if (LinesIntersection(x3, y3, x1, y1, y, &x_mas[c_mas], &x_mas[c_mas + 1], &match)) {
			c_mas += 2;
		}
		if (!c_mas) continue;
		x_min = x_max = x_mas[0];
		while (c_mas) {
			x_min = min(x_min, x_mas[c_mas - 2]);
			x_max = max(x_max, x_mas[c_mas - 1]);
			c_mas -= 2;
		}
		LCD_DrawLine(lcd, x_min, y, x_max, y, color);
	}
}

/* Рисует окружность с заданным центром и радиусом */
void LCD_DrawCircle(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t r, uint32_t color)
{
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	LCD_DrawPixel(lcd, x0, y0 + r, color);
	LCD_DrawPixel(lcd, x0, y0 - r, color);
	LCD_DrawPixel(lcd, x0 + r, y0, color);
	LCD_DrawPixel(lcd, x0 - r, y0, color);

	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		LCD_DrawPixel(lcd, x0 + x, y0 + y, color);
		LCD_DrawPixel(lcd, x0 - x, y0 + y, color);
		LCD_DrawPixel(lcd, x0 + x, y0 - y, color);
		LCD_DrawPixel(lcd, x0 - x, y0 - y, color);

		LCD_DrawPixel(lcd, x0 + y, y0 + x, color);
		LCD_DrawPixel(lcd, x0 - y, y0 + x, color);
		LCD_DrawPixel(lcd, x0 + y, y0 - x, color);
		LCD_DrawPixel(lcd, x0 - y, y0 - x, color);
	}
}

/* Рисует закрашенную окружность с заданным центром и радиусом */
void LCD_DrawFilledCircle(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t r, uint32_t color)
{
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	LCD_DrawLine(lcd, x0 - r, y0, x0 + r, y0, color);

	while (x < y) {
		if (f >= 0)	{
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		LCD_DrawLine(lcd, x0 - x, y0 + y, x0 + x, y0 + y, color);
		LCD_DrawLine(lcd, x0 + x, y0 - y, x0 - x, y0 - y, color);

		LCD_DrawLine(lcd, x0 + y, y0 + x, x0 - y, y0 + x, color);
		LCD_DrawLine(lcd, x0 + y, y0 - x, x0 - y, y0 - x, color);
	}
}

/*
 * Выводит в заданную область дисплея блок памяти (изображение) по адресу в data:
 * x, y - координата левого верхнего угла области дисплея;
 * w, h - ширина и высота области дисплея;
 * data - указатель на блок памяти (изображение) для вывода на дисплей;
 * dma_use_flag - флаг, определяющий задействование DMA (0 - без DMA, !=0 - с DMA)
 */
void LCD_DrawImage(LCD_Handler* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *data, uint8_t dma_use_flag)
{
	if ((x >= lcd->Width) || (y >= lcd->Height) || (x + w - 1) >= lcd->Width || (y + h - 1) >= lcd->Height) return;
	LCD_SetActiveWindow(lcd, x, y, x + w - 1, y + h - 1);
	if (dma_use_flag) {
		LCD_WriteDataDMA(lcd, data, w * h);
	}
	else {
		LCD_WriteData(lcd, data, w * h);
	}
}

/* Читает в буфер data данные о цвете пикселей из окна дисплея с координатами левого верхнего угла (x, y), шириной w, высотой h.
* Доступны 2 режима работы со spi: полный дуплекс (full-duplex) и полудуплекс (half-duplex).
* Полный дуплекс применяется на дисплеях, подключаемых к МК по двум однонаправленным линиям данных: MOSI и MISO.
* Полудуплекс применяется для чтения данных с контроллеров дисплеев, содержащих только один вывод SDA, совмещающий линии
* in/out. Вывод CS в режиме полудуплекса СТРОГО ОБЯЗАТЕЛЕН, т.к. только после установки в высокий уровень этого сигнала
* контроллер дисплея выйдет из режима передачи данных MCU и будет готов к приему новых команд от MCU. При отсутствии
* вывода CS возврат в режим приема команд контроллером дисплея может быть осуществлен только после сброса (reset)
* контроллера дисплея, который в данном драйвере/библиотеке выполняется командой инициализации LCD_Init(lcd),
* где lcd - указатель на обработчик дисплея.
* Управление режимом работы процедуры осуществляется параметром SPI_HALF_DUPLEX_READ в файле display.h.
* Для включения полудуплекса:
* #define SPI_HALF_DUPLEX_READ	1
* Для включения полного дуплекса:
* #define SPI_HALF_DUPLEX_READ	0
* Скорость чтения, как правило, ниже (иногда значительно) скорости записи данных в контроллер поэтому введен параметр,
* определяющий скорость чтения из контроллера дисплея SPI_SPEED_DISPLAY_READ. Принимает значение от 0 до 7 и настраивается
* в display.h. Причем, 0 соответствует clk/2, 1 - clk/4, ... 7 - clk/256. Где clk - частота тактирования spi.
*/
void LCD_ReadImage(LCD_Handler* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *data)
{
	uint16_t x1 = x + w - 1, y1 = y + h - 1; //Определяем координаты правого нижнего угла окна
	//Проверяем параметры окна, т.к. за пределами размерности дисплея не работаем
	if (x >= lcd->Width || y >= lcd->Height || x1 >= lcd->Width || y1 >= lcd->Height) return;
	//Пересчет координат окна в зависимости от характеристик матрицы дисплея и контроллера.
	//Соответствующий offs в драйвере дисплея определяет эти характеристики, учитывая разницу размерностей,
	//начальное смещение матрицы дисплея относительно поля памяти контроллера дисплея и ориентацию изображения на матрице
	x += lcd->x_offs;
	y += lcd->y_offs;
	x1 += lcd->x_offs;
	y1 += lcd->y_offs;
	//Создаем управляющую строку для интерпретатора драйвера дисплея, которая укажет контроллеру дисплея,
	//что мы определяем область памяти и хотим прочитать эту область. Предварительно переведем дисплей в
	//режим цвета 18 бит, так как команда Memory Read (0x2E) должна работать только с режимом цвета 18 бит.
	uint8_t	set_win_and_read[] = { //Команда выбора режима цвета
								   LCD_UPR_COMMAND, 0x3A, 1, 0x66, //0x66 - 18-битный цвет, 0x55 - 16-битный
								   //Установка адресов, определяющих блок:
								   LCD_UPR_COMMAND, 0x2A, 4, 0, 0, 0, 0, //столбец
								   LCD_UPR_COMMAND, 0x2B, 4, 0, 0, 0, 0, //строка
								   //Команда Memory Read (читать память)
								   LCD_UPR_COMMAND, 0x2E, 0,
								   LCD_UPR_END	};
	//Вписываем в управляющую строку координаты заданного окна
	set_win_and_read[7]  = x >> 8;  set_win_and_read[8]  = x & 0xFF;
	set_win_and_read[9]  = x1 >> 8; set_win_and_read[10]  = x1 & 0xFF;
	set_win_and_read[14] = y >> 8;  set_win_and_read[15] = y & 0xFF;
	set_win_and_read[16] = y1 >> 8; set_win_and_read[17] = y1 & 0xFF;
	//Ждем, когда дисплей освободится и будет готов к приему новых команд и данных
	while (LCD_GetState(lcd) != LCD_STATE_READY) ;
	lcd->cs_control = 1; //Отключаем управление линией CS со стороны интерпретатора управляющих строк
	SPI_TypeDef *spi = lcd->spi_data.spi;
	uint32_t spi_param = spi->CR1; //Запоминаем параметры spi
	//Настраиваем spi (spi у нас уже выключен)
	//настройки полный дуплекс
	spi->CR1 &= ~(SPI_CR1_BIDIMODE |  //2 линии, однонаправленный режим
				  SPI_CR1_RXONLY   |  //прием и передача
				  SPI_CR1_CRCEN    |
				  SPI_CR1_BR_Msk   |  //Маска скорости spi
				  SPI_CR1_DFF); 	  //Ширина кадра 8 бит
	//Установим скорость spi для чтения дисплея.
	//Параметр SPI_SPEED_DISPLAY_READ настраивается в display.h
	//Дело в том, что согласно спецификаций на контроллеры дисплея, скорость
	//в режиме чтения из контроллера, как правило, ниже скорости в режиме записи данных
	//в контроллер.
	spi->CR1 |= (uint32_t)((SPI_SPEED_DISPLAY_READ & 7) << SPI_CR1_BR_Pos);
	//Отправляем через интерпретатор управляющую строку на контроллер дисплея
	//"Дергать" выводом CS после отправки команды 0x2E нельзя, т.к. контроллер
	//дисплея может "подумать", что мы хотим прерывать чтение.
	LCD_CS_LOW //Подключаем контроллер дисплея к МК
	LCD_String_Interpretator(lcd, set_win_and_read);
	LCD_DC_HI //Вывод DC контроллера дисплея установим в положении "данные". Но, мои эксперименты показывают,
	//что чтение работает и в положении "команда", что странно, т.к. согласно спецификации, первая команда, в т.ч.,
	//NOP должна прерывать операцию чтения памяти контроллера. В общем, чтение идет до тех пор, пока мы не прочитаем
	//всю выбранную нами область, либо пока тупо не прервем процесс чтения.
	uint32_t len = w * h; //Количество пикселей для чтения
	uint16_t *data_ptr = data; //Указатель на местоположение буфера для хранения пикселей
	uint8_t r, g, b; //Переменные с цветовыми составляющими
#if (SPI_HALF_DUPLEX_READ == 1) //Если используется полудуплекс, то скорректируем настройки spi
	//Настройки для полудуплекса (только прием):
	spi->CR1 |= SPI_CR1_BIDIMODE; //Двунаправленная линия данных
	spi->CR1 &= ~SPI_CR1_BIDIOE;  //Режим приема
	spi->CR1 ^= SPI_CR1_CPHA_Msk; //Согласно спецификации при приеме меняем фазу на противоположную (для полудуплекса)
#endif
	//Включаем spi. Для полудуплексного режима прием стартует сразу же, как только включим spi (пойдут такты от MCU)
	spi->CR1 |= SPI_CR1_SPE;
	//16 холостых тактов для подготовки контроллера дисплея к отправке данных MCU
	int i = 2;
	while (i--) {
#if (SPI_HALF_DUPLEX_READ == 0) //В полудуплексе при приеме заливать данные в DR для старта тактирования не надо
		LL_SPI_TransmitData8(spi, 0x00); //NOP
#endif
		while (!(spi->SR & SPI_SR_RXNE)) ; //Ожидаем прием ответа от контроллера дисплея
		r = LL_SPI_ReceiveData8(spi);
	}
	//------------------------------ Читаем данные о цвете len пикселей --------------------------
	while (len--) {
		//Считываем последовательно цветовые составляющие
		//По спецификации последовательность считываемых составляющих цветов заявлена r, g, b,
		//Если считываемые цвета будут не соответствовать фактическим, то снизьте скорость spi для чтения,
		//но иногда стабильности чтения помогает подтяжка к питанию линии MISO spi.
#if (SPI_HALF_DUPLEX_READ == 0)
		LL_SPI_TransmitData8(spi, 0x00); //NOP
#endif
		while (!(spi->SR & SPI_SR_RXNE)) ;//Ожидаем прием ответа от контроллера дисплея
		r = LL_SPI_ReceiveData8(spi);
#if (SPI_HALF_DUPLEX_READ == 0)
		LL_SPI_TransmitData8(spi, 0x00); //NOP
#endif
		while (!(spi->SR & SPI_SR_RXNE)) ;
		g = LL_SPI_ReceiveData8(spi);
#if (SPI_HALF_DUPLEX_READ == 0)
		LL_SPI_TransmitData8(spi, 0x00); //NOP
#endif
		while (!(spi->SR & SPI_SR_RXNE)) ;
		b = LL_SPI_ReceiveData8(spi);
		*data_ptr++ = LCD_Color(lcd, r, g, b); //Преобразуем цвет из R8G8B8 в R5G6B5 и запоминаем его
	}
	LCD_CS_HI //Отключаем контроллер дисплея от МК.
	  	  	  //В режиме полудуплекса, согласно спецификации, это будет еще и сигналом для
	  	  	  //переключения направления линии SDA на прием информации от MCU. Так что, без
	  	  	  //линии CS на дисплее НЕ ОБОЙТИСЬ.
#if (SPI_HALF_DUPLEX_READ == 0)
	while (spi->SR & SPI_SR_BSY) ; //Ждем когда spi освободится
											  //А в полудуплексе ждать не надо (см. спецификацию MCU),
											  //но надо дочитывать...
#endif
	spi->CR1 &= ~SPI_CR1_SPE; //spi выключаем
#if (SPI_HALF_DUPLEX_READ == 1)
	//Обязательное дочитывание после выключения spi для полудуплексного режима, иначе будет "не гуд"
	while (!(spi->SR & SPI_SR_RXNE)) ;
#endif
	spi->CR1 = spi_param; //Восстанавливаем параметры spi
	//Восстанавливаем 16-битный режим цвета
	lcd->cs_control = 0; //Включаем управление линией CS со стороны интерпретатора управляющих строк
	uint8_t	color_restore[]  = { LCD_UPR_COMMAND, 0x3A, 1, 0x55,
								 LCD_UPR_END };
	LCD_String_Interpretator(lcd, color_restore);
}

/*
 * Вывод на дисплей символа с кодом в ch, с начальными координатами координатам (x, y), шрифтом font, цветом color,
 * цветом окружения bgcolor.
 * modesym - определяет, как выводить символ:
 *    LCD_SYMBOL_PRINT_FAST - быстрый вывод с полным затиранием знакоместа;
 *    LCD_SYMBOL_PRINT_PSETBYPSET - вывод символа по точкам, при этом цвет окружения bgcolor игнорируется (режим наложения).
 * Ширина символа до 32 пикселей (4 байта на строку). Высота символа библиотекой не ограничивается.
 */
void LCD_WriteChar(LCD_Handler* lcd, uint16_t x, uint16_t y, char ch, FontDef *font, uint32_t txcolor, uint32_t bgcolor, LCD_PrintSymbolMode modesym)
{
	int i, j, k;
	uint32_t tmp = 0;
	const uint8_t *b = font->data;
	uint16_t color;
	uint16_t txcolor16 = LCD_Color_24b_to_16b(lcd, txcolor);
	uint16_t bgcolor16 = LCD_Color_24b_to_16b(lcd, bgcolor);
	ch = ch < font->firstcode || ch > font->lastcode ? 0: ch - font->firstcode;
	int bytes_per_line = ((font->width - 1) >> 3) + 1;
	if (bytes_per_line > 4) { //Поддержка ширины символов до 32 пикселей (4 байта на строку)
		return;
	}
	k = 1 << ((bytes_per_line << 3) - 1);
	b += ch * bytes_per_line * font->height;
	SPI_TypeDef *spi = lcd->spi_data.spi;
	if (modesym == LCD_SYMBOL_PRINT_FAST) {
		LCD_SetActiveWindow(lcd, x, y, x + font->width - 1, y + font->height - 1);
		LCD_CS_LOW
		LCD_DC_HI
		spi->CR1 &= ~SPI_CR1_SPE; // SPI выключаем, чтобы изменить параметры
		spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
					   SPI_CR1_RXONLY |   	//  Transmit only
					   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
					   SPI_CR1_DFF); 		//8-битная передача
		if (lcd->data_bus == LCD_DATA_16BIT_BUS) {
			spi->CR1 |= SPI_CR1_DFF; //16-битная передача
		}
		spi->CR1 |= SPI_CR1_SPE; // SPI включаем
		for (i = 0; i < font->height; i++) {
			if (bytes_per_line == 1)      { tmp = *((uint8_t*)b);  }
			else if (bytes_per_line == 2) { tmp = *((uint16_t*)b); }
			else if (bytes_per_line == 3) { tmp = (*((uint8_t*)b)) | ((*((uint8_t*)(b + 1))) << 8) |  ((*((uint8_t*)(b + 2))) << 16); }
			else { tmp = *((uint32_t*)b); }
			b += bytes_per_line;
			for (j = 0; j < font->width; j++)
			{
				color = (tmp << j) & k ? txcolor16: bgcolor16;
				while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
				if (lcd->data_bus == LCD_DATA_16BIT_BUS) {
					LL_SPI_TransmitData16(spi, color);
				}
				else {
					uint8_t color1 = color & 0xFF, color2 = color >> 8;
					LL_SPI_TransmitData8(spi, color1);
					while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
					LL_SPI_TransmitData8(spi, color2);
				}
			}
		}
		while (!(spi->SR & SPI_SR_TXE)) ; //ждем окончания передачи
		while (spi->SR & SPI_SR_BSY) ; //ждем когда SPI освободится
		//выключаем spi
		spi->CR1 &= ~SPI_CR1_SPE;
		LCD_CS_HI
	}
	else {
		for (i = 0; i < font->height; i++) {
			if (bytes_per_line == 1) { tmp = *((uint8_t*)b); }
			else if (bytes_per_line == 2) { tmp = *((uint16_t*)b); }
			else if (bytes_per_line == 3) { tmp = (*((uint8_t*)b)) | ((*((uint8_t*)(b + 1))) << 8) |  ((*((uint8_t*)(b + 2))) << 16); }
			else if (bytes_per_line == 4) { tmp = *((uint32_t*)b); }
			b += bytes_per_line;
			for (j = 0; j < font->width; j++) {
				if ((tmp << j) & k) {
					LCD_DrawPixel(lcd, x + j, y + i, txcolor);
				}
			}
		}
	}
}

//вывод строки str текста с позиции x, y, шрифтом font, цветом букв color, цветом окружения bgcolor
//modesym - определяет, как выводить текст:
//LCD_SYMBOL_PRINT_FAST - быстрый вывод с полным затиранием знакоместа
//LCD_SYMBOL_PRINT_PSETBYPSET - вывод по точкам, при этом цвет окружения bgcolor игнорируется (позволяет накладывать надписи на картинки)
void LCD_WriteString(LCD_Handler* lcd, uint16_t x, uint16_t y, const char *str, FontDef *font, uint32_t color, uint32_t bgcolor, LCD_PrintSymbolMode modesym)
{
	while (*str) {
		if (x + font->width > lcd->Width) {
			x = 0;
			y += font->height;
			if (y + font->height > lcd->Height) {
				break;
			}
		}
		LCD_WriteChar(lcd, x, y, *str, font, color, bgcolor, modesym);
		x += font->width;
		str++;
	}
	lcd->AtPos.x = x;
	lcd->AtPos.y = y;
}

inline uint16_t LCD_Color (LCD_Handler *lcd, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t color = (((uint16_t)r & 0xF8) << 8) | (((uint16_t)g & 0xFC) << 3) | (((uint16_t)b >> 3));
	if (lcd->data_bus == LCD_DATA_8BIT_BUS) {//8-битная передача
		color = (color >> 8) | ((color & 0xFF) << 8);
	}
	return color;
}

inline uint16_t LCD_Color_24b_to_16b(LCD_Handler *lcd, uint32_t color)
{
	uint8_t r = (color >> 16) & 0xff;
	uint8_t g = (color >> 8) & 0xff;
	uint8_t b = color & 0xff;
	return LCD_Color(lcd, r, g, b);
}
