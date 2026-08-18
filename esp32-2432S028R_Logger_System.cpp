#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SD.h>
#include <SPI.h>
#include <LittleFS.h>
#include <lvgl.h>
#include "LGFX_Driver.h"
#include "time.h"

#define PIN_RX 22
#define PIN_TX 27
#define BAUD_RATE 115200

TaskHandle_t gui_task;
SemaphoreHandle_t file_mutex;
SemaphoreHandle_t lvgl_mutex;
SemaphoreHandle_t server_mutex;

volatile float weight = 0;
volatile float temp_smaple = 0;
volatile float temp_oven = 0;

hw_timer_t* timer_read_data = NULL;
volatile bool flag_read_data = false;

void IRAM_ATTR change_flag() {
  flag_read_data = true;
}

void initial_timer() {
  timer_read_data = timerBegin(1000000);
  timerAttachInterrupt(timer_read_data, &change_flag);
  timerAlarm(timer_read_data, 10000, true, 0);
  timerStart(timer_read_data);
}

#define BUFFER_SIZE 64
char buffer[BUFFER_SIZE];

// -> GUI
  static lv_disp_draw_buf_t draw_buf;
  static lv_color_t buf[320*10];
  static lv_disp_drv_t disp_drv;

  static lv_indev_drv_t indev_drv;
  void my_touch_read(lv_indev_drv_t* indev, lv_indev_data_t* data);

  void initial_display();
  
  void create_main_window(lv_obj_t* scr);
  void create_gas_window(lv_obj_t* scr);
  void create_chart_window(lv_obj_t* scr);
  void create_settings_window(lv_obj_t* scr);

  void screen_cast();

  static void handler_button(lv_event_t *e);

  void task_gui(void* param) {
    initial_display();
    for (; ;) { // -> loop
      lv_tick_inc(5);
      lv_timer_handler();
      vTaskDelay(5);
    }
  }

  void my_flush_disp(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t width = area -> x2 - area -> x1 + 1;
    uint32_t height = area -> y2 - area -> y1 + 1;
    lcd.startWrite();
    lcd.setAddrWindow(area -> x1, area -> y1, width, height);
    lcd.writePixels(reinterpret_cast<lgfx::rgb565_t *>(color_p), width * height);
    lcd.endWrite();
    lv_disp_flush_ready(disp);
  }

  void my_touch_read(lv_indev_drv_t* indev, lv_indev_data_t* data) {
    uint16_t x;
    uint16_t y;

    if (lcd.getTouch(&x, &y)) {
      x = 319 - x; 
      data->state = LV_INDEV_STATE_PRESSED;
      data -> point.x = x;
      data -> point.y = y;
    } else {
      data -> state = LV_INDEV_STATE_RELEASED;
    }
  }

  /**
    В данном проекте будут использоваться 4 экрана, 
    каждый из экранов отражает одну из частей общей системы логирования, например: 
    
    screen_oven - это экран, который отображает такие показания, как вес пробы, 
    температуры внутри пробы и внутри самой печи; 

    screen_gas - этот экран отображает расход газов
    
    screen_chart - этот экран будет отображать сразу несколько графиков, 
    каждый график представляет из зависимость компонента логирования от времени

    screen_settings - это экран для настройки работы системы, при помощи этого экрана
    пользователь может выбирать формат работы логгера, а также хранилище данных, куда будут
    записываться сами данные 
  */

  lv_obj_t* screen_oven;
  lv_obj_t* screen_gas;
  lv_obj_t* screen_chart;
  lv_obj_t* screen_settings;

  void initial_display() {
    lcd.init();
    lcd.setRotation(1);

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf, NULL, (320 * 10));
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;

    disp_drv.flush_cb = my_flush_disp;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touch_read;
    lv_indev_drv_register(&indev_drv);

    lcd.fillScreen(TFT_BLACK);
    lcd.setTextColor(TFT_WHITE);

    lv_obj_t* scr = lv_scr_act();

    screen_oven = lv_obj_create(NULL);
    screen_gas = lv_obj_create(NULL);
    screen_chart = lv_obj_create(NULL);
    screen_settings = lv_obj_create(NULL);


    create_main_window(screen_oven); // -> наполнение экрана oven
    create_gas_window(screen_gas);
    create_chart_window(screen_chart);

    lv_scr_load(screen_oven);
  }

  lv_obj_t* create_panel(lv_obj_t* scr, 
                        lv_align_t align, 
                        int width, 
                        int height, 
                        int x_position, 
                        int y_position) {
    lv_obj_t* panel = lv_obj_create(scr);
    lv_obj_set_size(panel, width, height);
    lv_obj_align(panel, align, x_position, y_position);
    return panel;
  }

  lv_obj_t* create_label(lv_obj_t* scr,
                        const char* txt,
                        lv_align_t align,
                        int x_position,
                        int y_position) {
    lv_obj_t* label = lv_label_create(scr);
    lv_label_set_text(label, txt);
    lv_obj_align(label, align, x_position, y_position);
    return label;
  }

  lv_obj_t* create_button(lv_obj_t* scr, 
                          lv_align_t align, 
                          int width, 
                          int height, 
                          int x_position, 
                          int y_position) {
    lv_obj_t* button = lv_btn_create(scr);
    lv_obj_set_size(button, width, height);
    lv_obj_align(button, align, x_position, y_position);
    return button;
  }
  // -> window
    void create_main_window(lv_obj_t* scr) { // -> main window
      lv_obj_t* main_label = create_label(scr, "Direct Recover Oven", LV_ALIGN_TOP_MID, 0, 5);

      lv_obj_t* panel_temp_oven = create_panel(scr, LV_ALIGN_TOP_LEFT, 160, 70, 0, 30); 
      lv_obj_t* label_oven_temp = create_label(panel_temp_oven, "Temp Oven", LV_ALIGN_TOP_MID, 0, 0);

      lv_obj_t* panel_temp_sample = create_panel(scr, LV_ALIGN_TOP_RIGHT, 160, 70, 0, 30); 
      lv_obj_t* label_sample_temp = create_label(panel_temp_sample, "Temp Sample", LV_ALIGN_TOP_MID, 0, 0);

      lv_obj_t* panel_weight = create_panel(scr, LV_ALIGN_CENTER, 320, 70, 0, 15);
      lv_obj_t* label_weight = create_label(panel_weight, "Weight", LV_ALIGN_TOP_MID, 0, 0);

      lv_obj_t* panel_button = create_panel(scr, LV_ALIGN_BOTTOM_MID, 320, 70, 0, 0);
      lv_obj_clear_flag(panel_button, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_pad_all(panel_button, 0, LV_PART_MAIN);
      
      lv_obj_update_layout(scr);
      lv_area_t a;

      lv_obj_t* button_gas = create_button(panel_button, LV_ALIGN_LEFT_MID, 100, 70, -10, 0);
      lv_obj_t* label_gas_btn = create_label(button_gas, "Gas", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(button_gas, handler_button, LV_EVENT_CLICKED, (void*) "Gas");
      
      lv_obj_t* button_settings_btn = create_button(panel_button, LV_ALIGN_CENTER, 100, 70, 0, 0);
      lv_obj_t* label_settings_btn = create_label(button_settings_btn, "Chart", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(button_settings_btn, handler_button, LV_EVENT_CLICKED, (void*) "Chart");

      lv_obj_t* button_chart_btn = create_button(panel_button, LV_ALIGN_RIGHT_MID, 100, 70, 10, 0);
      lv_obj_t* label_chart_btn = create_label(button_chart_btn, "Settings", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(button_chart_btn, handler_button, LV_EVENT_CLICKED, (void*) "Settings");
    }

    void create_gas_window(lv_obj_t* scr) { // -> window for check gas consumption
      lv_obj_t* main_label_gas1_panel = create_label(scr, "Gas Flow", LV_ALIGN_TOP_MID, 0, 5);
      lv_obj_clear_flag(main_label_gas1_panel, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(main_label_gas1_panel, LV_SCROLLBAR_MODE_OFF);

      lv_obj_t* panel_gas1 = create_panel(scr, LV_ALIGN_TOP_MID, 320, 40, 0, 30);
      lv_obj_clear_flag(panel_gas1, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(panel_gas1, LV_SCROLLBAR_MODE_OFF);
      lv_obj_t* label_gas1 = create_label(panel_gas1, "H2", LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* label_value_gas1 = create_label(panel_gas1, "0.0", LV_ALIGN_LEFT_MID, 100, 0); // -> вынести за пределы данной функции(изменение значения)
      lv_obj_t* btn_gas1 = create_button(panel_gas1, LV_ALIGN_TOP_MID, 40, 20, 80, -5);
      lv_obj_set_style_bg_color(btn_gas1, lv_color_hex(0x0000CC), LV_PART_MAIN);
      lv_obj_t* status_label_gas1 = create_label(panel_gas1, "OFF", LV_ALIGN_RIGHT_MID, 0, 0); // -> вынести за пределы данной функции(изменение значения)

      lv_obj_t* panel_gas2 = create_panel(scr, LV_ALIGN_TOP_MID, 320, 40, 0, 70);
      lv_obj_clear_flag(panel_gas2, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(panel_gas2, LV_SCROLLBAR_MODE_OFF);
      lv_obj_t* label_gas2 = create_label(panel_gas2, "CO", LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* label_value_gas2 = create_label(panel_gas2, "0.0", LV_ALIGN_LEFT_MID, 100, 0);
      lv_obj_t* btn_gas2 = create_button(panel_gas2, LV_ALIGN_TOP_MID, 40, 20, 80, -5);
      lv_obj_set_style_bg_color(btn_gas2, lv_color_hex(0x0000CC), LV_PART_MAIN);
      lv_obj_t* status_label_gas2 = create_label(panel_gas2, "OFF", LV_ALIGN_RIGHT_MID, 0, 0);

      lv_obj_t* panel_gas3 = create_panel(scr, LV_ALIGN_TOP_MID, 320, 40, 0, 110);
      lv_obj_clear_flag(panel_gas3, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(panel_gas3, LV_SCROLLBAR_MODE_OFF);
      lv_obj_t* label_gas3 = create_label(panel_gas3, "CO2", LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* label_value_gas3 = create_label(panel_gas3, "0.0", LV_ALIGN_LEFT_MID, 100, 0);
      lv_obj_t* btn_gas3 = create_button(panel_gas3, LV_ALIGN_TOP_MID, 40, 20, 80, -5);
      lv_obj_set_style_bg_color(btn_gas3, lv_color_hex(0x0000CC), LV_PART_MAIN); // -> 0x33FF33 - зеленый
      lv_obj_t* status_label_gas3 = create_label(panel_gas3, "OFF", LV_ALIGN_RIGHT_MID, 0, 0);
      
      lv_obj_t* panel_gas4 = create_panel(scr, LV_ALIGN_BOTTOM_MID, 320, 40, 0, -50);
      lv_obj_clear_flag(panel_gas4, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(panel_gas4, LV_SCROLLBAR_MODE_OFF);
      lv_obj_t* label_gas4 = create_label(panel_gas4, "N2", LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_t* label_value_gas4 = create_label(panel_gas4, "0.0", LV_ALIGN_LEFT_MID, 100, 0);
      lv_obj_t* btn_gas4 = create_button(panel_gas4, LV_ALIGN_TOP_MID, 40, 20, 80, -5);
      lv_obj_set_style_bg_color(btn_gas4, lv_color_hex(0x0000CC), LV_PART_MAIN);
      lv_obj_t* status_label_gas4 = create_label(panel_gas4, "OFF", LV_ALIGN_RIGHT_MID, 0, 0);

      lv_obj_t* panel_menu = create_panel(scr, LV_ALIGN_BOTTOM_MID, 320, 50, 0, 0);
      lv_obj_clear_flag(panel_menu, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(panel_menu, LV_SCROLLBAR_MODE_OFF);

      lv_obj_t* btn_main_menu = create_button(panel_menu, LV_ALIGN_LEFT_MID, 100, 70, -10, 0);
      lv_obj_t* btn_label_main = create_label(btn_main_menu, "Oven", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(btn_main_menu, handler_button, LV_EVENT_CLICKED, (void*) "Oven");

      lv_obj_t* btn_chart = create_button(panel_menu, LV_ALIGN_CENTER, 100, 70, 0, 0);
      lv_obj_t* btn_label_chart = create_label(btn_chart, "Chart", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(btn_chart, handler_button, LV_EVENT_CLICKED, (void*) "Chart");

      lv_obj_t* btn_settings = create_button(panel_menu, LV_ALIGN_RIGHT_MID, 100, 70, 10, 0);
      lv_obj_t* btn_label_settings = create_label(btn_settings, "Settings", LV_ALIGN_RIGHT_MID, 0, 0);
      lv_obj_add_event_cb(btn_settings, handler_button, LV_EVENT_CLICKED, (void*) "Settings");
    }


    /**
      Экран графиков должен отображать сразу несколько графиков
      (P.S тз: 
        Помимо мнемосхем, отображающих текущие значения параметров опыта 
        (температура, вес, расход газов, расстояние, время опыта), 
        иметь возможность построения графиков изменения параметров за определенное время;
      )
      
      Поэтому данный экран должен отображать такие графики:
      1) y(t, °C) = x(t) -> изменение температуры от времени
      2) y(w, g) = x(t) -> изменение веса от времени
      3) y(g) = x(t) -> {
        изменение расхода H2 от времени; 
        изменение расхода CO от времени; 
        изменение расхода CO2 от времени; 
        изменение расхода N2 от времени
      } -> С учетом этого нужно понимать какие газы используются в опыте
    */

    lv_obj_t* lbl_name_chart;
    lv_obj_t* lbl_value;

    const char* arr_lbls[] = {
      "Temperature Chart",
      "Weight Chart",
      "Gas H2 Chart",
      "Gas CO Chart",
      "Gas CO2 Chart",
      "Gas N2 Chart"
    };

    uint8_t current_element_arr_lbs = 0;
    #define ALL_ELEMENTS_LBLS 6


    lv_obj_t* chart;
    lv_chart_series_t* series_chart; // -> series for charts
    uint32_t value_series[] = {100, 200, 300, 150, 320, 310, 220, 186, 163, 101};
    uint8_t current_element_arr_value_series = 0;
    #define ALL_ELEMENTS_ARRAY_VALUE_SERIES 10 

    lv_obj_t* lbl_sensor;
    const char* lbl_txt_sensor_arr[] = {
      "Temp Value",
      "Weight Value",
      "Gas_H2 Value",
      "Gas_CO Value",
      "Gas_CO2 Value",
      "Gas_N2 Value"
    };
    uint8_t current_element_lbl_txt_sensor_arr = 0;
    #define ALL_ELEMENTS_ARRAY_LBL_SENSOR 6 


    lv_obj_t* create_chart(lv_obj_t* scr, 
                           int width, 
                           int height,
                           lv_align_t align,
                           int x_position,
                           int y_position) {
      lv_obj_t* chart = lv_chart_create(scr);
      
      lv_obj_set_size(chart, width, height);
      lv_obj_align(chart, align, x_position, y_position);
      
      lv_chart_set_type(chart, LV_CHART_TYPE_LINE);

      lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1200);
      lv_chart_set_point_count(chart, 30);


      lv_chart_set_div_line_count(chart, 6, 8);

      lv_obj_set_style_bg_color(chart, lv_color_hex(0x15191E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
      return chart;
    }


    void create_chart_window(lv_obj_t* scr) { // -> window chart
      lv_obj_t* panel_chart = create_panel(scr, LV_ALIGN_CENTER, 320, 185, 0, -30);
      lv_obj_clear_flag(panel_chart, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(panel_chart, LV_SCROLLBAR_MODE_OFF);
      
      lv_obj_t* lbl_main = create_label(panel_chart, "Chart Window", LV_ALIGN_TOP_MID, 0, -10);

      lbl_name_chart = create_label(panel_chart, arr_lbls[current_element_arr_lbs], LV_ALIGN_TOP_MID, 0, 10); // -> labek change
      
      chart = create_chart(panel_chart, 250, 100, LV_ALIGN_CENTER, 0, 0); // -> chart create 
      series_chart = lv_chart_add_series(chart, 
                                         lv_palette_main(LV_PALETTE_RED), 
                                         LV_CHART_AXIS_PRIMARY_Y
      ); // -> series for chart

      for (int i = 1; i < 10; i++) {
        lv_chart_set_next_value(chart, series_chart, 
                                random(((i * value_series[current_element_arr_value_series]) - 50), 
                                (i * value_series[current_element_arr_value_series]))
        );
      } // -> тестовая серия для графика => нужно обновлять!

      lv_chart_refresh(chart);


      lv_obj_t* btn_back_chart = create_button(panel_chart, LV_ALIGN_BOTTOM_LEFT, 40, 25, 70, 5);
      lv_obj_t* lbl_btn_back = create_label(btn_back_chart, "Back", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(btn_back_chart, handler_button, LV_EVENT_CLICKED, (void*) "Back");


      lv_obj_t* btn_next_chart = create_button(panel_chart, LV_ALIGN_BOTTOM_LEFT, 40, 25, 20, 5);
      lv_obj_t* lbl_btn_next = create_label(btn_next_chart, "Next", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(btn_next_chart, handler_button, LV_EVENT_CLICKED, (void*) "Next");


      lbl_sensor = create_label(panel_chart, lbl_txt_sensor_arr[current_element_lbl_txt_sensor_arr], LV_ALIGN_BOTTOM_RIGHT, -50, 0);
      lbl_value = create_label(panel_chart, "0.0", LV_ALIGN_BOTTOM_RIGHT, -20, 0);

      lv_obj_t* panel_btn = create_panel(scr, LV_ALIGN_BOTTOM_MID, 320, 60, 0, 0);
      lv_obj_clear_flag(panel_btn, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scrollbar_mode(panel_chart, LV_SCROLLBAR_MODE_OFF);

      lv_obj_t* btn_main_window = create_button(panel_btn, LV_ALIGN_LEFT_MID, 100, 60, -10, 0);
      lv_obj_t* lbl_btn_main_window = create_label(btn_main_window, "Oven", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(btn_main_window, handler_button, LV_EVENT_CLICKED, (void*) "Oven");

      lv_obj_t* btn_gas_window = create_button(panel_btn, LV_ALIGN_CENTER, 100, 60, 0, 0);
      lv_obj_t* lbl_btn_gas_window = create_label(btn_gas_window, "Gas", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(btn_gas_window, handler_button, LV_EVENT_CLICKED, (void*) "Gas");

      lv_obj_t* btn_settings_window = create_button(panel_btn, LV_ALIGN_RIGHT_MID, 100, 60, 10, 0);
      lv_obj_t* lbl_btn_settings_window = create_label(btn_settings_window, "Settings", LV_ALIGN_CENTER, 0, 0);
      lv_obj_add_event_cb(btn_settings_window, handler_button, LV_EVENT_CLICKED, (void*) "Settings");

    }


    /**
      Для того чтобы создать окно настроек логгера разберем то, что пользователь может настраивать:
      
      1) Так как у данного логера есть возможность записывать данные в память -> LittleFS, 
      также пользователь может записывать данные на отдельный носитель -> SD,
      а также пользователь может записывать данные в Google таблицы -> Google Sheet

      Из этого списка следует, что пользователь решает то, куда будут записываться данные

      P.S. Запись в Google Sheets может быть реализована только тогда, 
      когда пользователь подключен к инету => Пользователь должен видеть источники wifi-сети, 
      а также пользователь должен иметь возможность подключаться к выбранной сети

      2) Опционально, пользователь может управлять яркостью дисплея

      3) Пользователь сам должен решать с какой частотой данные будут обрабатываться(
        получение данных,
        отображение данных,
        запись данных
      ) => следовательно данный пункт тоже должен быть включен в систему настроек

      Итог: На данный момент окно настроек - settings_window будет иметь 3-4 окна.
            Первое окно - Настройка подключения к сети wifi -> Должна быть возможность пропустить данную
            настройку
            P.S. добавить уровень сигнала сети wifi

            Второе окно - Настройка записи данных -> Должна быть реализована возможность выбора места хранилища,
            а также должна быть реализована логика проверки подключения к сети, если выбран вариант записи данных
            в Google-Sheets

            Третье окно - Настройка скорости считывания данных -> Пользователю должна быть дана возможность
            выбирать скорость считывания данных с датчиков. 
            P.S. Данная логика должна быть реализована при помощи прерываний

            Четвертое окно(Опционально) - Настройка яркости экрана -> Пользователю предоставляется возможность
            управлять яркостью экрана.
    **/
    void create_settings_window(lv_obj_t* scr) { // -> window settings

      // -> TODO: Реализовать экран для настроек

    }
  // -> window

  // -> handler button
    static void handler_button(lv_event_t *e) {
      const char* name = (const char*) lv_event_get_user_data(e); // -> Определение нажатие кнопки по lbl_btn


      /***
        т.к при нажатии на кнопку lv_event_get_user_data(e); 
        параметр данной функции будет содержать label кнопки на которую мы нажали. 
        Далее идет сравнение строк C при помощи метода strcmp(). Данная функци вернет 0, 
        если строки полностью одинаковы, в противном случае она вернет значения отличные
        от 0
      */


      if (strcmp(name, "Oven") == 0) {
        lv_scr_load(screen_oven);
      } else if (strcmp(name, "Gas") == 0) {
        lv_scr_load(screen_gas);
      } else if (strcmp(name, "Chart") == 0) {
        lv_scr_load(screen_chart);
      } else if (strcmp(name, "Settings") == 0) {
        lv_scr_load(screen_settings);
      }


      if (strcmp(name, "Next") == 0) {
        // Serial.println("btn with lbl Next - input");

        current_element_arr_lbs++;

        if (current_element_arr_lbs >= ALL_ELEMENTS_LBLS) {
          current_element_arr_lbs = 0;
        }

        lv_label_set_text(lbl_name_chart, arr_lbls[current_element_arr_lbs]);


        // -> очистить грфик
        lv_chart_set_all_value(chart, series_chart, LV_CHART_POINT_NONE);
        lv_chart_refresh(chart);

        current_element_arr_value_series++;
        if (current_element_arr_value_series >= ALL_ELEMENTS_ARRAY_VALUE_SERIES) {
          current_element_arr_value_series = 0;
        }


        for (int i = 1; i < 10; i++) {
          lv_chart_set_next_value(chart, series_chart, 
                                  random(((i * value_series[current_element_arr_value_series]) - 50), 
                                  (i * value_series[current_element_arr_value_series]))
          );
        }


        current_element_lbl_txt_sensor_arr++;
        if (current_element_lbl_txt_sensor_arr >= ALL_ELEMENTS_ARRAY_LBL_SENSOR) {
          current_element_lbl_txt_sensor_arr = 0;
        }
        lv_label_set_text(lbl_sensor, lbl_txt_sensor_arr[current_element_lbl_txt_sensor_arr]);

        // -> TODO: сделать окно этапов опытов + реализация смены этапов 

      } else if (strcmp(name, "Back") == 0) {



        if (current_element_arr_lbs <= 0) {
          current_element_arr_lbs = ALL_ELEMENTS_LBLS;
        }

        current_element_arr_lbs--;

        lv_label_set_text(lbl_name_chart, arr_lbls[current_element_arr_lbs]);


        lv_chart_set_all_value(chart, series_chart, LV_CHART_POINT_NONE);
        lv_chart_refresh(chart);

        if (current_element_arr_value_series <= 0) {
          current_element_arr_value_series = ALL_ELEMENTS_ARRAY_VALUE_SERIES;
        }

        current_element_arr_value_series--;

        for (int i = 1; i < 10; i++) {
          lv_chart_set_next_value(chart, series_chart, 
                                  random(((i * value_series[current_element_arr_value_series]) - 50), 
                                  (i * value_series[current_element_arr_value_series]))
          );
        }

        if (current_element_lbl_txt_sensor_arr <= 0) {
          current_element_lbl_txt_sensor_arr = ALL_ELEMENTS_ARRAY_LBL_SENSOR;
        }
        current_element_lbl_txt_sensor_arr--;
        lv_label_set_text(lbl_sensor, lbl_txt_sensor_arr[current_element_lbl_txt_sensor_arr]);

      }
    }
  // -> handler button

// -> GUI

// -> File System
  void initial_file_system() {
    if (!LittleFS.begin(true)) {
      Serial.println("LOGGER_SYSTEM: Error initial File System");
      return;
    }
    Serial.println("LOGGER_SYSTEM: Successfully Operation");
  }

  void create_dir(fs::FS &fs, const char* path) {
    if (fs.mkdir(path)) {
      Serial.println("LOGGER_SYSTEM: Successfully Create Direction");
    } else {
      Serial.println("LOGGER_SYSTEM: Error Create Direction");
      return;
    }
  }

  void write_fs(fs::FS &fs, const char* path, const char* data) {
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
      Serial.println("LOGGER_SYSTEM: Do Not Open File For Write Data");
      return;
    }
    if (file.print(data)) {
      Serial.println("LOGGER_SYSTEM: Data Successfully Write To File");
    }
    file.close();
  } 

  void delete_file(fs::FS &fs, const char* path) {
    if (fs.remove(path)) {
      Serial.println("LOGGER_SYSTEM: Data Successfully Remove");
    } else {
      Serial.println("LOGGER_SYSTEM: Error Delete Data");
      return;
    }
  }
// -> File System

// -> SD System 
// -> SD System

// -> server
  AsyncWebServer server(80);

  IPAddress ip(192, 168, 2, 1);
  IPAddress getaway(192, 168, 2, 1);
  IPAddress subnet(255, 255, 255, 0);

  const char* ssid = "LoggerTorex";
  const char* password = "1234567890asDF";

  const char index_html[] PROGMEM = R"rawliteral(
    <!DOCTYPE>
    <html>
      <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>Oven Logger</title>
      </head>
      <body>
        <p>Temperature In Oven</p>
        <p>0</p>
        <p>Temperature In The Sample</p>
        <p>0</p>
        <p>Weight</p>
        <p>0</p>
      </body>
      <script>
        // -> скрипт для показаний
      </script>
    </html>
  )rawliteral";

  void initial_server() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
      request -> send(200, "text/html", index_html);
    });

    server.on("/get_temp_oven", HTTP_GET, [](AsyncWebServerRequest* request) {
      
    });

    server.on("/get_temp_sample", HTTP_GET, [](AsyncWebServerRequest* request) {
    });

    server.on("/get_weight", HTTP_GET, [](AsyncWebServerRequest* request) {
    });
    server.begin();
  }
// -> server

void initial_serial_port() {
  Serial1.begin(BAUD_RATE, SERIAL_8N1, PIN_RX, PIN_TX);
  delay(1000);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  file_mutex = xSemaphoreCreateMutex();
  lvgl_mutex = xSemaphoreCreateMutex();
  server_mutex = xSemaphoreCreateMutex();

  initial_serial_port();

  xTaskCreatePinnedToCore(
    task_gui,
    "Task GUI",
    10000,
    NULL,
    1,
    &gui_task,
    0
  );

  initial_file_system();

  initial_timer();
}

void loop() {
  if (flag_read_data) {
    flag_read_data = false;
      if (Serial1.available()) {
      size_t read_bytes = Serial1.readBytesUntil('\n', buffer, BUFFER_SIZE - 1);
    }
  }
}
