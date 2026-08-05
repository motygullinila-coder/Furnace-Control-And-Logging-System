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
      Serial.printf("Touch: %3d %3d\n", x, y);
      x = 319 - x; 
      data->state = LV_INDEV_STATE_PRESSED;
      data -> point.x = x;
      data -> point.y = y;
    } else {
      data -> state = LV_INDEV_STATE_RELEASED;
    }
  }

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
    create_main_window(scr);
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

  void create_main_window(lv_obj_t* scr) {
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
    lv_obj_t* label_settings_btn = create_label(button_settings_btn, "Settings", LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(button_settings_btn, handler_button, LV_EVENT_CLICKED, (void*) "Settings");

    lv_obj_t* button_chart_btn = create_button(panel_button, LV_ALIGN_RIGHT_MID, 100, 70, 10, 0);
    lv_obj_t* label_chart_btn = create_label(button_chart_btn, "Chart", LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(button_chart_btn, handler_button, LV_EVENT_CLICKED, (void*) "Chart");
  }

  // -> handler button
    static void handler_button(lv_event_t *e) {
      const char* name = (const char*) lv_event_get_user_data(e);
      lv_obj_t *obj = lv_event_get_target(e);
      Serial.printf("Clicked: %s (%p)\n", name, obj);
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
