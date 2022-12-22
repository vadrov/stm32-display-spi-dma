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
#define MAX(x, y) (x > y ? x : y)
#define MIN(x, y) (x < y ? x : y)

#define LCD_CS_LOW		if (lcd->spi_data.cs_port) { lcd->spi_data.cs_port->BSRR = (uint32_t)lcd->spi_data.cs_pin << 16U; }
#define LCD_CS_HI		if (lcd->spi_data.cs_port) { lcd->spi_data.cs_port->BSRR = lcd->spi_data.cs_pin; }
#define LCD_DC_LOW		{lcd->spi_data.dc_port->BSRR = (uint32_t)lcd->spi_data.dc_pin << 16U; }
#define LCD_DC_HI		{lcd->spi_data.dc_port->BSRR = lcd->spi_data.dc_pin; }

#define DISABLE_IRQ		__disable_irq(); __DSB(); __ISB();
#define ENABLE_IRQ		__enable_irq();

LCD_Handler *LCD = 0;

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
	while (lcd)
	{
		//получаем параметры DMA потока дисплея
		dma_ct = lcd->spi_data.dma_tx.dma;
		stream_ct = lcd->spi_data.dma_tx.stream;
		//проверка на соответствие текущего потока DMA потоку, к которому привязан i-тый дисплей
		if (dma_ct == dma_x && stream_ct == stream)
		{
			if (lcd->spi_data.cs_port) //управление по cs поддерживается?
			{
				//на выводе cs дисплея низкий уровень?
				if (lcd->spi_data.cs_port->ODR & lcd->spi_data.cs_pin) //проверяем состояние пина выходного регистра порта
				{
					lcd = (LCD_Handler *)lcd->next;		   //если высокий уровень cs, то не этот дисплей активен
					continue;							   //и переходим к следующему
				}
			}
			//указатель на поток: aдрес контроллера + смещение
			DMA_Stream_TypeDef *dma_TX = ((DMA_Stream_TypeDef *)((uint32_t)((uint32_t)dma_x + STREAM_OFFSET_TAB[stream])));
			//выключаем поток DMA
			dma_TX->CR &= ~DMA_SxCR_EN;
			while (dma_TX->CR & DMA_SxCR_EN) {__NOP();} //ждем отключения потока
			if (lcd->size_mem) //если переданы не все данные из памяти, то перезапускаем DMA и выходим из прерывания
			{
				if (lcd->size_mem > 65535)
				{
					dma_TX->NDTR = 65535;
					lcd->size_mem -= 65535;
				}
				else
				{
					dma_TX->NDTR = lcd->size_mem;
					lcd->size_mem = 0;
				}
				//включаем поток DMA
				dma_TX->CR |= (DMA_SxCR_EN);
				return;
			}
#ifdef LCD_DYNAMIC_MEM
			//очищаем буфер дисплея
			if (lcd->tmp_buf)
			{
		    	//так как память выделяется динамически, то на всякий случай,
				//чтобы не было коллизий, запретим прерывания перед освобождением памяти
				DISABLE_IRQ
				free(lcd->tmp_buf);
				lcd->tmp_buf = 0;
				ENABLE_IRQ
			}
#endif
			//запрещаем SPI принимать запросы от DMA
			lcd->spi_data.spi->CR2 &= ~SPI_CR2_TXDMAEN;
			while (lcd->spi_data.spi->SR & SPI_SR_BSY) { __NOP(); } //ждем пока SPI освободится
			//отключаем дисплей от MK (притягиваем вывод CS дисплея к высокому уровню)
			if (!lcd->cs_control) LCD_CS_HI;
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
	if (lcd_dc == lcd_write_command) LCD_DC_LOW
	else LCD_DC_HI
	spi->DR = data; //команда
	while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
	while (spi->SR & SPI_SR_BSY)    { __NOP(); } //ждем когда SPI освободится
}

void LCD_HardWareReset (LCD_Handler* lcd)
{
	if (lcd->spi_data.reset_port)
	{
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
	while (LCD_GetState(lcd) == LCD_STATE_BUSY) { __NOP(); } //ждем когда дисплей освободится
	LCD_CS_LOW
	spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
				   SPI_CR1_RXONLY |   	//  Transmit only
				   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
				   SPI_CR1_DFF); 		//установим 8-битную передачу
	spi->CR1 |= SPI_CR1_SPE; // SPI включаем
	while (1)
	{
		switch (*str++)
		{
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
				LCD_CS_HI
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
	if (hdma->dma)
	{
		DMA_Stream_TypeDef *dma_x = ((DMA_Stream_TypeDef *)((uint32_t)((uint32_t)hdma->dma + STREAM_OFFSET_TAB[hdma->stream])));
		dma_x->CR &= ~DMA_SxCR_EN; //отключаем канал DMA
		while(dma_x->CR & DMA_SxCR_EN) {__NOP();} //ждем отключения канала
		if (lcd->data_bus == LCD_DATA_8BIT_BUS)
		{
			dma_x->CR &= ~(DMA_SxCR_MSIZE | DMA_SxCR_PSIZE);
			dma_x->CR |= LL_DMA_MDATAALIGN_BYTE | LL_DMA_PDATAALIGN_BYTE;
		}
		else if (lcd->data_bus == LCD_DATA_16BIT_BUS)
		{
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
	uint16_t max_res = MAX(resolution1, resolution2);
	uint16_t min_res = MIN(resolution1, resolution2);
	if (orientation==PAGE_ORIENTATION_PORTRAIT || orientation==PAGE_ORIENTATION_PORTRAIT_MIRROR)
	{
		lcd->Width = min_res;
		lcd->Height = max_res;
		lcd->Width_Controller = width_controller;
		lcd->Height_Controller = height_controller;
		if (orientation==PAGE_ORIENTATION_PORTRAIT)
		{
			lcd->x_offs = 0;
			lcd->y_offs = 0;
		}
		else
		{
			lcd->x_offs = lcd->Width_Controller - lcd->Width;
			lcd->y_offs = lcd->Height_Controller - lcd->Height;
		}
	}
	else if (orientation==PAGE_ORIENTATION_LANDSCAPE || orientation==PAGE_ORIENTATION_LANDSCAPE_MIRROR)
	{
		lcd->Width = max_res;
		lcd->Height = min_res;
		lcd->Width_Controller = height_controller;
		lcd->Height_Controller = width_controller;
		if (orientation==PAGE_ORIENTATION_LANDSCAPE)
		{
			lcd->x_offs = 0;
			lcd->y_offs = lcd->Height_Controller - lcd->Height;
		}
		else
		{
			lcd->x_offs = lcd->Width_Controller - lcd->Width;
			lcd->y_offs = 0;
		}
	}
	else
	{
		LCD_Delete(lcd);
		return 0;
	}

	if (lcd->Width_Controller < lcd->Width ||
		lcd->Height_Controller < lcd->Height ||
		init==NULL ||
		set_win==NULL )
	{
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
#ifndef LCD_DYNAMIC_MEM
	lcd->tmp_buf = lcd->display_work_buffer;
#endif
	if (!lcds) return lcd;
	LCD_Handler *prev = lcds;
	while (prev->next)
	{
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
	if (lcd)
	{
#ifdef LCD_DYNAMIC_MEM
		if (lcd->tmp_buf)	free(lcd->tmp_buf);
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
uint8_t LCD_GetBackLight(LCD_Handler* lcd)
{
	return lcd->bkl_data.bk_percent;
}

//возвращает ширину дисплея, пиксели
uint16_t LCD_GetWidth(LCD_Handler* lcd)
{
	return lcd->Width;
}

//возвращает высоту дисплея, пиксели
uint16_t LCD_GetHeight(LCD_Handler* lcd)
{
	return lcd->Height;
}

//возвращает статус дисплея: занят либо свободен (требуется для отправки новых данных на дисплей)
//дисплей занят, если занято spi, к которому он подключен
inline LCD_State LCD_GetState(LCD_Handler* lcd)
{
	//if ((lcd->spi_data.spi->SR & SPI_SR_BSY) || !lcd->DMA_TX_Complete) return LCD_STATE_BUSY;
	//если включен spi, к которому подключен дисплей, то дисплей занят
	if (lcd->spi_data.spi->CR1 & SPI_CR1_SPE) return LCD_STATE_BUSY;
	return LCD_STATE_READY;
}

//управление подсветкой
void LCD_SetBackLight(LCD_Handler* lcd, uint8_t bk_percent)
{
	if (bk_percent > 100) bk_percent = 100;
	lcd->bkl_data.bk_percent = bk_percent;
	//подсветка с использованием PWM
	if (lcd->bkl_data.htim_bk)
	{
		//вычисляем % яркости, как часть от периода счетчика
		uint32_t bk_value = lcd->bkl_data.htim_bk->ARR*bk_percent/100;
		//задаем скважность PWM конкретного канала
		switch(lcd->bkl_data.channel_htim_bk)
		{
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
		if (!(lcd->bkl_data.htim_bk->CR1 & TIM_CR1_CEN))
		{
			//включаем канал
			lcd->bkl_data.htim_bk->CCER |= lcd->bkl_data.channel_htim_bk;
			//включаем счетчик
			lcd->bkl_data.htim_bk->CR1 |= TIM_CR1_CEN;
		}
	}
	//подсветка без PWM (просто вкл./выкл.), если таймер с PWM недоступен
	else if (lcd->bkl_data.blk_port)
	{
		if (bk_percent)
			lcd->bkl_data.blk_port->BSRR = lcd->bkl_data.blk_pin;
		else
			lcd->bkl_data.blk_port->BSRR = (uint32_t)lcd->bkl_data.blk_pin << 16U;
	}
}

//перевод дисплея в "спящий режим" (выключение отображения дисплея и подсветки)
void LCD_SleepIn(LCD_Handler* lcd)
{
	//подсветка с использованием PWM
	if (lcd->bkl_data.htim_bk)
	{
		//выключаем подсветку, установив нулевую скважность
		switch(lcd->bkl_data.channel_htim_bk)
		{
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
	else if (lcd->bkl_data.blk_port)
	{
		lcd->bkl_data.blk_port->BSRR = (uint32_t)lcd->bkl_data.blk_pin << 16U;
	}
	if (lcd->SleepIn_callback)
		LCD_String_Interpretator(lcd, lcd->SleepIn_callback());
}

//вывод дисплея из "спящего режима" (включение отображения дисплея и подсветки)
void LCD_SleepOut(LCD_Handler* lcd)
{
	if (lcd->SleepOut_callback)
		LCD_String_Interpretator(lcd, lcd->SleepOut_callback());
	//включение подсветки
	LCD_SetBackLight(lcd, lcd->bkl_data.bk_percent);
}

//установка на дисплее окна вывода
void LCD_SetActiveWindow(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	LCD_String_Interpretator(lcd, lcd->SetActiveWindow_callback(x1+lcd->x_offs, y1+lcd->y_offs, x2+lcd->x_offs, y2+lcd->y_offs));
}

//вывод блока данных на дисплей
void LCD_WriteData(LCD_Handler *lcd, uint16_t *data, uint32_t len)
{
	SPI_TypeDef *spi = lcd->spi_data.spi;
	while (LCD_GetState(lcd) == LCD_STATE_BUSY) { __NOP(); } //ждем когда дисплей освободится
	if (!lcd->cs_control) LCD_CS_LOW
	if (!lcd->dc_control) LCD_DC_HI
	spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
				   SPI_CR1_RXONLY |   	//  Transmit only
				   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
				   SPI_CR1_DFF); 		//8-битная передача
	if (lcd->data_bus == LCD_DATA_16BIT_BUS)
	{
		spi->CR1 |= SPI_CR1_DFF; //16-битная передача
	}
	spi->CR1 |= SPI_CR1_SPE; // SPI включаем
	if (lcd->data_bus == LCD_DATA_16BIT_BUS)
	{
		while (len)
		{
			spi->DR = *data++; //записываем данные в регистр
			while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
			len--;
		}
	}
	else
	{
		len *= 2;
		uint8_t *data1 = (uint8_t *)data;
		while (len)
		{
			spi->DR = *data1++; //записываем данные в регистр
			while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
			len--;
		}
	}
	while (spi->SR & SPI_SR_BSY) { __NOP(); } //ждем когда SPI освободится
	if (!lcd->cs_control) LCD_CS_HI
	//выключаем spi
	spi->CR1 &= ~SPI_CR1_SPE;
}

//вывод блока данных на дисплей с DMA
void LCD_WriteDataDMA(LCD_Handler *lcd, uint16_t *data, uint32_t len)
{
	if (lcd->spi_data.dma_tx.dma)
	{
		if (lcd->data_bus == LCD_DATA_8BIT_BUS)
		{
			len *= 2;
		}
		SPI_TypeDef *spi = lcd->spi_data.spi;
		while (LCD_GetState(lcd) == LCD_STATE_BUSY) { __NOP(); } //ждем когда дисплей освободится
		if (!lcd->cs_control) LCD_CS_LOW
		if (!lcd->dc_control) LCD_DC_HI
		lcd->size_mem = len;
		spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
					   SPI_CR1_RXONLY |   	//  Transmit - передача
					   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
					   SPI_CR1_DFF); 		//8-битная передача
		if (lcd->data_bus == LCD_DATA_16BIT_BUS)
		{
			spi->CR1 |= SPI_CR1_DFF; //16-битная передача
		}
		spi->CR1 |= SPI_CR1_SPE; // SPI включаем
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
		if (len <= 65535)
		{
			dma_TX->NDTR = (uint32_t)len; //размер передаваемых данных
			lcd->size_mem = 0;
		}
		else
		{
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
	if (lcd->data_bus == LCD_DATA_16BIT_BUS)
	{
		spi->CR1 |= SPI_CR1_DFF; //16-битная передача
	}
	spi->CR1 |= SPI_CR1_SPE; // SPI включаем
	if (lcd->spi_data.dma_tx.dma)
	{
		if (lcd->data_bus == LCD_DATA_8BIT_BUS)
		{
			len *= 2;
		}
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
		dma_TX->M0AR = (uint32_t)&lcd->fill_color; //источник память - адрес буфера исходящих данных
		dma_TX->CR &= ~DMA_SxCR_PINC; //инкремент адреса периферии отключен
		dma_TX->CR &= ~DMA_SxCR_MINC; //инкремент адреса памяти отключен
		if (len <= 65535)
		{
			dma_TX->NDTR = (uint32_t)len; //размер передаваемых данных
			lcd->size_mem = 0;
		}
		else
		{
			dma_TX->NDTR = 65535;
			lcd->size_mem = len - 65535;
		}
		dma_TX->CR |= (DMA_SxCR_EN); //включение канала передачи (старт DMA передачи)
		return;
	}
	if (lcd->data_bus == LCD_DATA_16BIT_BUS)
	{
		while(len)
		{
			spi->DR = color16; //записываем данные в регистр
			while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
			len--;
		}
	}
	else
	{
		uint8_t color1 = color16 & 0xFF, color2 = color16 >> 8;
		while(len)
		{
			spi->DR = color1;
			while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
			spi->DR = color2;
			while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
			len--;
		}
	}
	while (spi->SR & SPI_SR_BSY) { __NOP(); } //ждем когда SPI освободится
	if (!lcd->cs_control) LCD_CS_HI
	//выключаем spi
	spi->CR1 &= ~SPI_CR1_SPE;
}

void LCD_Fill(LCD_Handler* lcd, uint32_t color)
{
	LCD_FillWindow(lcd, 0, 0, lcd->Width - 1, lcd->Height - 1, color);
}

void LCD_DrawPixel(LCD_Handler* lcd, uint16_t x, uint16_t y, uint32_t color)
{
	if (x >= lcd->Width || y >= lcd->Height)	return;
	LCD_SetActiveWindow(lcd, x, y, x, y);
	uint16_t color1 = LCD_Color_24b_to_16b(lcd, color);
	LCD_WriteData(lcd, &color1, 1);
}

void LCD_DrawLine(LCD_Handler* lcd, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint32_t color)
{
	if(x0==x1 || y0==y1)
	{
		uint16_t tmp = x1;
		if (x0 > x1)
		{
			x1 = x0;
			x0 = tmp;
		}
		tmp = y1;
		if (y0 > y1)
		{
			y1 = y0;
			y0 = tmp;
		}
		LCD_FillWindow(lcd, x0, y0, x1, y1, color);
		return;
	}
	uint16_t swap;
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

void LCD_DrawRectangle(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
	LCD_DrawLine(lcd, x1, y1, x2, y1, color);
	LCD_DrawLine(lcd, x1, y1, x1, y2, color);
	LCD_DrawLine(lcd, x1, y2, x2, y2, color);
	LCD_DrawLine(lcd, x2, y1, x2, y2, color);
}

void LCD_DrawCircle(LCD_Handler* lcd, uint16_t x0, uint16_t y0, uint8_t r, uint32_t color)
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

//выводит на экран область памяти (изображение) по адресу в data
//x, y - координата верхнего левого края
void LCD_DrawImage(LCD_Handler* lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *data, uint8_t dma_use_flag)
{
	if ((x >= lcd->Width) || (y >= lcd->Height) || (x + w - 1) >= lcd->Width || (y + h - 1) >= lcd->Height) return;
	LCD_SetActiveWindow(lcd, x, y, x + w - 1, y + h - 1);
	if (dma_use_flag)
		LCD_WriteDataDMA(lcd, (uint16_t *)data, w * h);
	else
		LCD_WriteData(lcd, (uint16_t *)data, w * h);
}

//вывод символа ch текста с позиции x, y, шрифтом font, цветом букв color, цветом окружения bgcolor
//modesym - определяет, как выводить символ:
//LCD_SYMBOL_PRINT_FAST - быстрый вывод с полным затиранием знакоместа
//LCD_SYMBOL_PRINT_PSETBYPSET - вывод символа по точкам, при этом цвет окружения bgcolor игнорируется (режим наложения)
//ширина символа до 32 пикселей (4 байта на строку)
void LCD_WriteChar(LCD_Handler* lcd, uint16_t x, uint16_t y, char ch, FontDef *font, uint32_t txcolor, uint32_t bgcolor, LCD_PrintSymbolMode modesym)
{
	int i, j, k;
	uint32_t tmp = 0;
	const uint8_t *b = font->data;
	uint16_t color;
	uint16_t txcolor16 = LCD_Color_24b_to_16b(lcd, txcolor);
	uint16_t bgcolor16 = LCD_Color_24b_to_16b(lcd, bgcolor);
	ch = ch < font->firstcode || ch > font->lastcode ? 0: ch - font->firstcode;
	int bytes_per_line = ((font->width-1)>>3) + 1;
	k = 1<<((bytes_per_line<<3) - 1);
	b +=  ch * bytes_per_line * font->height;
	SPI_TypeDef *spi = lcd->spi_data.spi;
	if (modesym == LCD_SYMBOL_PRINT_FAST)
	{
		LCD_SetActiveWindow(lcd, x, y, x + font->width - 1, y + font->height - 1);
		LCD_CS_LOW
		LCD_DC_HI
		spi->CR1 &= ~SPI_CR1_SPE; // SPI выключаем, чтобы изменить параметры
		spi->CR1 &= ~ (SPI_CR1_BIDIMODE |  	//здесь задаем режим
					   SPI_CR1_RXONLY |   	//  Transmit only
					   SPI_CR1_CRCEN | 		//выключаем аппаратный расчет CRC
					   SPI_CR1_DFF); 		//8-битная передача
		if (lcd->data_bus == LCD_DATA_16BIT_BUS)
		{
			spi->CR1 |= SPI_CR1_DFF; //16-битная передача
		}
		spi->CR1 |= SPI_CR1_SPE; // SPI включаем
		for (i = 0; i < font->height; i++)
		{
			if (bytes_per_line == 1) { tmp = *((uint8_t*)b); }
			else if (bytes_per_line == 2) { tmp = *((uint16_t*)b); }
			else if (bytes_per_line == 3) { tmp = *((uint8_t*)b); tmp += (*((uint16_t*)(b+1)))>>8; }
			else if (bytes_per_line == 4) { tmp = *((uint32_t*)b); }
			b += bytes_per_line;
			for (j = 0; j < font->width; j++)
			{
				color = (tmp << j) & k ? txcolor16: bgcolor16;
				while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
				if (lcd->data_bus == LCD_DATA_16BIT_BUS)
					spi->DR = color;
				else
				{
					uint8_t color1 = color & 0xFF, color2 = color >> 8;
					spi->DR = color1;
					while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
					spi->DR = color2;
				}
			}
		}
		while (!(spi->SR & SPI_SR_TXE)) { __NOP(); } //ждем окончания передачи
		while (spi->SR & SPI_SR_BSY) { __NOP(); } //ждем когда SPI освободится
		//выключаем spi
		spi->CR1 &= ~SPI_CR1_SPE;
		LCD_CS_HI
	}
	else
	{
		for (i = 0; i < font->height; i++)
		{
			if (bytes_per_line == 1) { tmp = *((uint8_t*)b); }
			else if (bytes_per_line == 2) { tmp = *((uint16_t*)b); }
			else if (bytes_per_line == 3) { tmp = *((uint8_t*)b); tmp += (*((uint16_t*)(b+1)))>>8; }
			else if (bytes_per_line == 4) { tmp = *((uint32_t*)b); }
			b += bytes_per_line;
			for (j = 0; j < font->width; j++)
			{
				if ((tmp << j) & k)
				{
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
	while (*str)
	{
		if (x + font->width > lcd->Width)
		{
			x = 0;
			y += font->height;
			if (y + font->height > lcd->Height)
			{
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

void LCD_DrawFilledRectangle(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
	LCD_FillWindow(lcd, x1, y1, x2, y2, color);
}

void LCD_DrawTriangle(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint32_t color)
{
	LCD_DrawLine(lcd, x1, y1, x2, y2, color);
	LCD_DrawLine(lcd, x2, y2, x3, y3, color);
	LCD_DrawLine(lcd, x3, y3, x1, y1, color);
}

void LCD_DrawFilledTriangle(LCD_Handler* lcd, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, uint32_t color)
{
	int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0,
			yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0, numpixels = 0,
			curpixel = 0;

	deltax = ABS(x2 - x1);
	deltay = ABS(y2 - y1);
	x = x1;
	y = y1;

	if (x2 >= x1)
	{
		xinc1 = 1;
		xinc2 = 1;
	}
	else
	{
		xinc1 = -1;
		xinc2 = -1;
	}

	if (y2 >= y1)
	{
		yinc1 = 1;
		yinc2 = 1;
	}
	else
	{
		yinc1 = -1;
		yinc2 = -1;
	}
	if (deltax >= deltay)
	{
		xinc1 = 0;
		yinc2 = 0;
		den = deltax;
		num = deltax / 2;
		numadd = deltay;
		numpixels = deltax;
	}
	else
	{
		xinc2 = 0;
		yinc1 = 0;
		den = deltay;
		num = deltay / 2;
		numadd = deltax;
		numpixels = deltay;
	}
	for (curpixel = 0; curpixel <= numpixels; curpixel++)
	{
		LCD_DrawLine(lcd, x, y, x3, y3, color);

		num += numadd;
		if (num >= den)
		{
			num -= den;
			x += xinc1;
			y += yinc1;
		}
		x += xinc2;
		y += yinc2;
	}
}

void LCD_DrawFilledCircle(LCD_Handler* lcd, int16_t x0, int16_t y0, int16_t r, uint32_t color)
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
	LCD_DrawLine(lcd, x0 - r, y0, x0 + r, y0, color);

	while (x < y)
	{
		if (f >= 0)
		{
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

inline uint16_t LCD_Color (LCD_Handler *lcd, uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t color = (((uint16_t)r & 0xF8) << 8) | (((uint16_t)g & 0xFC) << 3) | (((uint16_t)b >> 3));
	if (lcd->data_bus == LCD_DATA_8BIT_BUS) //8-битная передача
	{
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
