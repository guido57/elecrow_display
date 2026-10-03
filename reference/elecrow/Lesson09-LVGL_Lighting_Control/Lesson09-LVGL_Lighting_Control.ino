/**
 * @file Lesson09-LVGL_Lighting_Control.ino
 * @brief Teaching example that demonstrates how to control the board LED from an LVGL interface.
 *
 * Comments emphasize program structure, hardware intent, and call order
 * while preserving the original executable behavior.
 */

/**
 * IMPORTANT:
 * This code requires the "ESP32_Display_Panel" library.
 * Before uploading, you MUST configure the following file in your libraries folder:
 * [Arduino_Library_Path]/ESP32_Display_Panel/esp_panel_drivers_conf.h
 * 
 * 1. Enable MIPI-DSI: #define ESP_PANEL_DRIVERS_BUS_USE_MIPI_DSI   (1)
 * 2. Set LCD Driver:  #define ESP_PANEL_DRIVERS_LCD_USE_EK79007    (1)
 * 3. Set Touch:       #define ESP_PANEL_DRIVERS_TOUCH_USE_GT911    (1)
 */
/*---------------------------------------------------------------
 * Dependencies
 * Libraries and board interfaces used by this lesson.
 *--------------------------------------------------------------*/
#include "board_config.h"   // board pin define
#include <Arduino.h>        // Arduino core library. Must be placed at the very top to ensure recognition of Arduino APIs
#include <HTTPClient.h>
#include <WiFi.h>

#include <string.h>         // C string lib
#include <esp_log.h>        // ESP-IDF logging library
#include <esp_err.h>        // ESP-IDF error codes
#include <esp_ldo_regulator.h>      // ESP32-P4 specific LDO management

#include "esp_panel_drivers_conf.h"
#include "esp_panel_board_custom_conf.h"
#include "ESP_Panel_Library.h"

#include <lvgl.h>
#include "lvgl_port.h"
#include "secrets.h"

using namespace esp_panel::drivers;
using namespace esp_panel::board;
/*---------------------------------------------------------------
 * Configuration constants
 * Compile-time settings and reusable logging helpers.
 *--------------------------------------------------------------*/
#define PRINTF_ORIGINAL(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__);
#define PRINTF_PRINT(fmt, ...)    Serial.print(fmt);
#define PRINTF_LN(fmt, ...)       Serial.println(fmt);

#define PRINTF_ERROR(fmt, ...)      do { \
                                        Serial.print("ERROR"); \
                                        Serial.printf(fmt, ##__VA_ARGS__); \
                                        Serial.print("\r\n"); \
                                    } while(0)  
#define PRINTF_WARN(fmt, ...)       do { \
                                        Serial.print("WARN: "); \
                                        Serial.printf(fmt, ##__VA_ARGS__); \
                                        Serial.print("\r\n"); \
                                    } while(0)  
#define PRINTF_INFO(fmt, ...)       do { \
                                        Serial.print("INFO: "); \
                                        Serial.printf(fmt, ##__VA_ARGS__); \
                                        Serial.print("\r\n"); \
                                    } while(0)  
#define PRINTF_DEBUG(fmt, ...)      do { \
                                        Serial.print("DEBUG: "); \
                                        Serial.printf(fmt, ##__VA_ARGS__); \
                                        Serial.print("\r\n"); \
                                    } while(0)

#define MAIN_INFO(fmt, ...)         PRINTF_INFO(fmt, ##__VA_ARGS__)   // Info level log macro
#define MAIN_ERROR(fmt, ...)        PRINTF_ERROR(fmt, ##__VA_ARGS__)  // Error level log macro

#define LV_COLOR_RED        lv_color_make(0xFF, 0x00, 0x00) // LVGL Red
#define LV_COLOR_GREEN      lv_color_make(0x00, 0xFF, 0x00) // LVGL Green
#define LV_COLOR_BLUE       lv_color_make(0x00, 0x00, 0xFF) // LVGL Blue
#define LV_COLOR_WHITE      lv_color_make(0xFF, 0xFF, 0xFF) // LVGL White
#define LV_COLOR_BLACK      lv_color_make(0x00, 0x00, 0x00) // LVGL Black
#define LV_COLOR_GRAY       lv_color_make(0x80, 0x80, 0x80) // LVGL gray
#define LV_COLOR_YELLOW     lv_color_make(0xFF, 0xFF, 0x00) // LVGL yellow
/*---------------------------------------------------------------
 * Shared lesson state
 * State shared by callbacks, tasks, and Arduino entry points.
 *--------------------------------------------------------------*/
static const char *TAG = "TOUCH_APP";  // Tag for logging messages

// Owns the display and touch devices after successful panel initialization.
ESP_Panel *panel = nullptr;
static lv_obj_t *temperature_label = nullptr;
static lv_obj_t *garden_temperature_label = nullptr;
static lv_obj_t *humidity_label = nullptr;
static lv_obj_t *status_label = nullptr;


/* Button callback function - turn on LED */
/*---------------------------------------------------------------
 * Lesson helper functions
 * Keep related operations together so the execution flow is easy to follow.
 *--------------------------------------------------------------*/
/**
 * @brief Turn on the LED when LVGL dispatches the ON-button event.
 *
 * @param e LVGL event that triggered the callback.
 * @return None.
 * @note Called by LVGL when the user activates the ON button.
 */
static void btn_on_click_event(lv_event_t *e)
{
    (void)e;
    digitalWrite(PIN_LED, LED_ON);  // Turn on LED on GPIO48
    MAIN_INFO("LED turned ON");
}

/* Button callback function - turn off LED */
/**
 * @brief Turn off the LED when LVGL dispatches the OFF-button event.
 *
 * @param e LVGL event that triggered the callback.
 * @return None.
 * @note Called by LVGL when the user activates the OFF button.
 */
static void btn_off_click_event(lv_event_t *e)
{
    (void)e;
    digitalWrite(PIN_LED, LED_OFF); // Turn off LED on GPIO48
    MAIN_INFO("LED turned OFF");
}

/* Create LED control UI */
/**
 * @brief Build the LVGL controls used to switch the LED on and off.
 *
 * Parameters: None.
 * @return None.
 * @note Called by the lesson workflow when this helper operation is required.
 */
static void create_sensor_ui(void)
{
    // Lock LVGL: ensure thread-safe operations
    if (lvgl_port_lock(-1) != true) {  // 0 means non-blocking wait for the lock (timeout = 0)
        MAIN_ERROR("LVGL lock failed");  // Print error if lock fails
        return;  // Exit function
    }
    // Create main screen
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), LV_PART_MAIN);  // Set white background

    // Create title label
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Garden climate");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 50);
    // Font size
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);

    temperature_label = lv_label_create(scr);
    lv_label_set_text(temperature_label, "Home Temperature: --.- C");
    lv_obj_set_style_text_font(temperature_label, &lv_font_montserrat_24, 0);
    lv_obj_align(temperature_label, LV_ALIGN_CENTER, 0, -60);

    garden_temperature_label = lv_label_create(scr);
    lv_label_set_text(garden_temperature_label, "Garden Temperature: --.- C");
    lv_obj_set_style_text_font(garden_temperature_label, &lv_font_montserrat_24, 0);
    lv_obj_align(garden_temperature_label, LV_ALIGN_CENTER, 0, -5);

    humidity_label = lv_label_create(scr);
    lv_label_set_text(humidity_label, "Garden Humidity: --.- %");
    lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_24, 0);
    lv_obj_align(humidity_label, LV_ALIGN_CENTER, 0, 50);

    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Connecting to Wi-Fi...");
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -30);

    lvgl_port_unlock();
}

static bool read_home_assistant_state(const char *entity_id, String &state)
{
    HTTPClient http;
    const String url = String(HA_BASE_URL) + "/api/states/" + entity_id;
    if (!http.begin(url)) {
        return false;
    }
    http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
    http.addHeader("Content-Type", "application/json");
    const int response_code = http.GET();
    if (response_code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    const String payload = http.getString();
    http.end();
    const int key = payload.indexOf("\"state\":\"");
    if (key < 0) {
        return false;
    }
    const int start = key + 9;
    const int end = payload.indexOf('"', start);
    if (end < 0) {
        return false;
    }
    state = payload.substring(start, end);
    return true;
}

static void sensor_task(void *)
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        if (lvgl_port_lock(-1)) {
            lv_label_set_text(status_label, "Connecting to Wi-Fi...");
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (true) {
        String temperature;
        String garden_temperature;
        String humidity;
        const bool temperature_ok = read_home_assistant_state(HA_TEMPERATURE_ENTITY, temperature);
        const bool garden_temperature_ok = read_home_assistant_state(HA_GARDEN_TEMPERATURE_ENTITY, garden_temperature);
        const bool humidity_ok = read_home_assistant_state(HA_HUMIDITY_ENTITY, humidity);

        if (lvgl_port_lock(-1)) {
            if (temperature_ok) {
                lv_label_set_text_fmt(temperature_label, "Home Temperature: %s C", temperature.c_str());
            }
            if (garden_temperature_ok) {
                lv_label_set_text_fmt(garden_temperature_label, "Garden Temperature: %s C", garden_temperature.c_str());
            }
            if (humidity_ok) {
                lv_label_set_text_fmt(humidity_label, "Garden Humidity: %s %%", humidity.c_str());
            }
            lv_label_set_text(status_label, (temperature_ok && garden_temperature_ok && humidity_ok) ? "Updated from Home Assistant" : "Home Assistant request failed");
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

#define C6_EN_GPIO 32

static void resetC6()
{
    Serial.println("Reset ESP32-C6...");

    pinMode(C6_EN_GPIO, OUTPUT);

    // ESP32-C6 EN è attivo alto:
    // LOW = reset, HIGH = funzionamento
    digitalWrite(C6_EN_GPIO, LOW);
    delay(100);

    digitalWrite(C6_EN_GPIO, HIGH);
    delay(500);

    Serial.println("ESP32-C6 released from reset");
}

/*---------------------------------------------------------------
 * Arduino entry points
 * The Arduino runtime calls setup() once and loop() repeatedly.
 *--------------------------------------------------------------*/
/**
 * @brief Initialize the hardware and services needed to control the board LED from an LVGL interface.
 *
 * Parameters: None.
 * @return None.
 * @note Called once by the Arduino runtime after reset.
 */
void setup() {

    // Initialize the default Serial for debugging (UART0)
    Serial.begin(115200);

  resetC6();

    // --- Power Configuration (LDO3 for MIPI D-PHY) ---
    // ESP32-P4's MIPI D-PHY requires specific voltage to function.
    // LDO3 is typically routed to the MIPI power rail on P4 hardware.
    esp_err_t err = ESP_OK;
    esp_ldo_channel_handle_t ldo3_handle = NULL;
    esp_ldo_channel_config_t ldo3_cfg = {
        .chan_id = 3,           // LDO Channel 3
        .voltage_mv = 2500,     // Set to 2500mV (2.5V)
    };

    Serial.println("Initializing LDO3 to 2.5V...");
    err = esp_ldo_acquire_channel(&ldo3_cfg, &ldo3_handle);
    if (err != ESP_OK) {
        Serial.printf("LDO3 Power Error: %s\n", esp_err_to_name(err));
    } else {
        Serial.println("LDO3 Power enabled successfully.");
    }
    
    // --- Power Configuration (LDO4 for I2C/touch pull up) ---
    esp_ldo_channel_handle_t ldo4_handle = NULL;
    esp_ldo_channel_config_t ldo4_cfg = {
        .chan_id = 4,           // LDO Channel 4
        .voltage_mv = 3300,     // Set to 3300mV (3.3V)
    };

    Serial.println("Initializing LDO4 to 3.3V...");
    err = esp_ldo_acquire_channel(&ldo4_cfg, &ldo4_handle);
    if (err != ESP_OK) {
        Serial.printf("LDO4 Power Error: %s\n", esp_err_to_name(err));
    } else {
        Serial.println("LDO4 Power enabled successfully.");
    }

    // --- Initialize Display and Touch Panel ---
    Board *board = new Board();
    // Initialize the bus (MIPI-DSI) and the devices (EK79007 & GT911)
    Serial.println("Initializing Panel (EK79007 + GT911)...");
    assert(board->init());
#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = board->getLCD();
    // When avoid tearing function is enabled, the frame buffer number should be set in the board driver
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#endif
    assert(board->begin());
    Serial.println("Display and Touch system online.");

    Serial.println("Initializing LVGL");
    lvgl_port_init(board->getLCD(), board->getTouch());

    Serial.println("Creating UI");

    create_sensor_ui();
    xTaskCreate(sensor_task, "ha_sensor", 6144, nullptr, 4, nullptr);
}

/**
 * @brief Continue the runtime workflow used to control the board LED from an LVGL interface.
 *
 * Parameters: None.
 * @return None.
 * @note Called repeatedly by the Arduino runtime after setup() returns.
 */
void loop() {
  
    delay(1000);
}
