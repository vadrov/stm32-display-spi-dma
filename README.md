 Copyright (C) 2019 - 2022, VadRov, all right reserved.
 
 [h1][b]Библиотека управления дисплеями по SPI с DMA. Release 1.4[/b][/h1]
 
 <p>Управление подключенным дисплеем с интерфейсом spi к микроконтроллеру семейства stm32f4 с поддержкой DMA, плавным изменением яркости подсветки
 посредством pwm. Поддерживаются дисплеи как с выводом CS, так и без него. Доступен выбор механизма управления выделением памяти: статическое или динамическое (см. файл display.h)</p>
 <p>В библиотеке (папка Display_spi_LL) представлены низкоуровневые драйвера для дисплеев с контроллерами
 st7789 и ili9341. Для подключения дисплея по spi c иным контроллером необходимо по примеру этих драйверов написать
 свой низкоуровневый драйвер со строками инициализации и т.п., обратившись к спецификации соответствующего
 контроллера дисплея.</p>
 <p>Доступны графические примитивы (точка, линия, прямоугольник и т.д.) и вывод текста. Доступно подключение шрифтов пользователя.</p>
 <p>Библиотека не использует HAL, а базируется на CMSIS и LL.</p>
 <p>В качестве примера, в коде проекта, созданного в среде STM32CudeIDE, представлено подключение дисплеев с контроллерами 
 ST7789 и ILI9341 к микроконтроллеру STM32F401CCU6 по SPI с DMA. Демонстрируется преимущество использования DMA.</p>
 <p>Как использовать библиотеку и настроить проект в среде STM32CudeIDE подробно рассказано в видео:</p>
 [![Watch the video](https://img.youtube.com/vi/8tIJ16riJqo/maxresdefault.jpg)](https://youtu.be/8tIJ16riJqo)
 <p>Upd.: Замечание к видео (там старый релиз библиотеки). Новый релиз библиотеки требует:</p>
 <ul>
 <li>Настройки DMA не в режиме Circular, как в видео, а в режиме Normal.</li>
 <li>Создание обработчика нового дисплея осуществляется функцией LCD_DisplayAdd, создающей и добавляющей дисплей в список дисплеев. 
 Этот список объявлен в библиотеке глобальной переменной LCD. После первого вызова указанной функции необходимо переназначать эту переменную, т.е
 записать, например, такой код:</li>
 <p><b><i>LCD = LCD_DisplayAdd (LCD, параметры дисплея...);</i></b> - для варианта с динамическим выделением памяти</p>
 <p><b><i>LCD = LCD_DisplayAdd (LCD, &lcd1, параметры дисплея...);</i></b> - для варианта со статическим выделением памяти</p>
 <p>В демо-проекте (см. файл main.c) показаны варианты инициализации дисплея при использовании динамического и статического выделения памяти</p>
 <p>Автор: <b>VadRov</b></p>
 <p>Контакты: <a href="https://www.youtube.com/c/VadRov">Youtube</a> <a href="https://zen.yandex.ru/vadrov">Дзен</a> <a href="https://vk.com/vadrov">VK</a> <a href="https://t.me/vadrov_channel">Telegram</a>
