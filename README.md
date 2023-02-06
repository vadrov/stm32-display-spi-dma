Copyright (C) 2019 - 2022, VadRov, all right reserved.
 
# Библиотека управления дисплеями по SPI с DMA. Release 1.4
Управление подключенным дисплеем с интерфейсом spi к микроконтроллеру семейства stm32f4 с поддержкой DMA, плавным изменением яркости подсветки посредством pwm. Поддерживаются дисплеи как с выводом CS, так и без него. Доступен выбор механизма управления выделением памяти: статическое или динамическое (см. файл display.h).
 
В библиотеке (папка Display) представлены низкоуровневые драйвера для дисплеев с контроллерами st7789 и ili9341. Для подключения дисплея по spi c иным контроллером необходимо по примеру этих драйверов написать свой низкоуровневый драйвер со строками инициализации и т.п., обратившись к спецификации соответствующего контроллера дисплея.
 
Доступны графические примитивы (точка, линия, прямоугольник и т.д.), вывод текста, вывод блока данных (изображения) в заданную область экрана. Доступно подключение шрифтов пользователя (в составе библиотеки их 3).
  
Библиотека не использует HAL, а базируется на CMSIS и LL.
 
В качестве примера, в коде проекта, созданного в среде STM32CudeIDE, представлено подключение дисплеев с контроллерами ST7789 и ILI9341 к микроконтроллеру STM32F401CCU6 по SPI с DMA. Демонстрируется преимущество использования DMA.
 
Как использовать библиотеку и настроить проект в среде STM32CudeIDE подробно рассказано в видео:
[![Watch the video](https://img.youtube.com/vi/8tIJ16riJqo/maxresdefault.jpg)](https://youtu.be/8tIJ16riJqo)
Upd.: Замечание к видео (там старый релиз библиотеки). Новый релиз библиотеки требует:
- Настройки DMA не в режиме Circular, как в видео, а в режиме Normal.
- Создание обработчика нового дисплея осуществляется функцией LCD_DisplayAdd, создающей и добавляющей дисплей в список дисплеев. 
Этот список объявлен в библиотеке глобальной переменной LCD. После первого вызова указанной функции необходимо переназначать эту (LCD) переменную, т.е
записать такой код:
1. Для варианта с динамическим выделением памяти
```c
LCD = LCD_DisplayAdd (LCD, параметры дисплея...);
```
2. Для варианта со статическим выделением памяти
```c
LCD_Handler lcd1;
LCD = LCD_DisplayAdd (LCD, &lcd1, параметры дисплея...);
``` 
Получить адрес обработчика созданного первого дисплея можно для обоих вариантов так (хотя, для 2 варианта адрес обработчика заведомо изместен: &lcd1):
```c
LCD_Handler *lcd = LCD;
```
Таким образом, lcd будет указывать на адрес созданного обработчика дисплея, он же первый дисплей в списке дисплеев LCD. Если требуется подключить еще один дисплей и создать обработчик для него, то после выполнения вышеуказанных действий, следует выполнить следующее: 
1. Для варианта с динамическим выделением памяти:
```c
LCD_Handler *lcd2 = LCD_DisplayAdd (LCD, параметры второго дисплея...);
``` 
2. Для варианта со статическим выделением памяти
```c
LCD_Handler lcd_2, *lcd2;
lcd2 = LCD_DisplayAdd (LCD, &lcd_2, параметры дисплея...);
``` 
Таким образом, получим указатель на обработчик второго дисплея lcd2. Причем, для второго варианта (статическое выделение памяти) при обращении к обработчику дисплея  запись &lcd_2 эквивалентана записи lcd2. Также указатель на второй дисплей можно получить так:
```c
LCD_Handler *lcd2_ptr = LCD->next;
``` 
В демо-проекте (см. файл main.c) показаны варианты инициализации дисплея при использовании динамического и статического выделения памяти.

**Если есть проблемы, вызванные потерей связи с контроллером дисплея (зависания дисплея, "каша" и т.п.), то проверьте есть ли подтяжка к питанию (pull_up) линий sck и mosi.** Например, настройка gpio, используемых spi, в виде:
```c
  /* SPI1 GPIO Configuration
  PA5   ------> SPI1_SCK  
  PA7   ------> SPI1_MOSI */
  GPIO_InitStruct.Pin = LCD_SCL_Pin|LCD_SDA_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;   /* Подтяжка к питанию есть */
  GPIO_InitStruct.Alternate = LL_GPIO_AF_5;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);
``` 
**Если не помогает подтяжка к питанию**, то причина потерь связи может быть вызвана большой скоростью spi, которая не поддерживается контроллером дисплея либо длинными проводниками, соединяющими дисплей с микроконтроллером, наводками от каких-либо помехосоздающих устройств, плохим контактом и т.п. 

**В отдельных случаях подтяжка к питанию наоборот может приводить к проблемам.** Например, если дисплейный модуль на контроллере дисплея ili9341 и контроллере тачскрина xpt2046 подключить к одному spi и сделать подтяжку к питанию сигнальных линий spi, то в лучшем случае получим на дисплеи артефакты, а в худшем - зависание дисплея или полный отказ работы.

**Дисплей может не запускаться из-за неправильной полярности тактового сигнала spi** (см. в настройках spi параметр CPOL - Clock Polarity). Как правило, дисплей на контроллере st7789 работает при значении CPOL = high, а на контроллере ili9341 при значении CPOL = low (может работать и на high, но не все модели и не всегда стабильно, признак неправильной полярности - дисплей стартует через раз/два при сбросе микроконтроллера).

Проблемы связи также могут быть вызваны **пульсирующим питанием модуля дисплея**. Возможно, в параллель питающих проводников дисплея потребуется установить электролитический конденсатор, например, емкостью около 100 мкф. 

**Также проблемы могут быть вызваны неправильным питающим напряжением дисплея.** Так, например, популярные дисплеи с контроллерами ili9341 имеют на плате встроенный преобразователь напряжения 3,3 В, и должны запитываться от напряжения около 5 В. При попытке запитать эти модули от напряжения 3,3 В, выработанные преобразователем платы микроконтроллера, можно получить нестабильную работу дисплея на скорости spi свыше 10 - 20 Мбит/с. В тоже время, следует помнить, что популярные IPS дисплеи 1,3' 240х240 px на базе контроллера st7789 питаются напряжением 3.3 В. 

**Всегда сверяйте с документацией питающее напряжение модуля дисплея!**

**Рекомендую проводить пробное подключение дисплея на скорости около 10 Мбит/с.** Добившись стабильной  работы дисплея на этой скорости (многократный сброс не вызывает ошибок в работе, дисплей не подвисает, артефактов нет и т.п.), можно поэтапно повышать скорость (уменьшать делитель частоты spi, параметр Prescaler).

Автор: **VadRov**

Контакты: [Youtube](https://www.youtube.com/c/VadRov) [Дзен](https://zen.yandex.ru/vadrov) [VK](https://vk.com/vadrov) [Telegram](https://t.me/vadrov_channel)

Донат: [Поддержать автора](https://yoomoney.ru/to/4100117522443917)
