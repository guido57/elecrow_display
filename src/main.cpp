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
#include <ArduinoJson.h>
#include "ani_flame.h"
#include <Preferences.h>
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
static const char *THERMOSTAT_BASE_URL = "http://192.168.1.243:2048";
static const uint32_t THERMOSTAT_REFRESH_MS = 5000;
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
static lv_obj_t *brightness_label = nullptr;
static lv_obj_t *wifi_state_label = nullptr;
static lv_obj_t *wifi_ssid_label = nullptr;
static lv_obj_t *wifi_signal_label = nullptr;
static lv_obj_t *wifi_ip_label = nullptr;
static Preferences brightness_preferences;
static Board *board = nullptr;
static lv_obj_t *temperature_arc = nullptr;
static lv_obj_t *garden_temperature_arc = nullptr;
static lv_obj_t *humidity_arc = nullptr;
static lv_obj_t *thermostat_mode_label = nullptr;
static lv_obj_t *manual_setpoint_label = nullptr;
static lv_obj_t *thermostat_buttons[3] = {};
static lv_obj_t *manual_setpoint_slider = nullptr;
static lv_obj_t *boiler_flame_gif = nullptr;
static String thermostat_mode = "Thermostat";
struct Schedule {
    const char *name;
    float temperature;
    uint8_t start_hour;
    uint8_t start_minute;
    uint8_t end_hour;
    uint8_t end_minute;
};
static Schedule schedules[4] = {
    {"Morning", 21, 6, 30, 8, 30},
    {"Day", 19, 8, 30, 18, 30},
    {"Evening", 21, 18, 30, 20, 30},
    {"Night", 17, 20, 30, 6, 30},
};
static lv_obj_t *schedule_temperature_labels[4] = {};
static lv_obj_t *schedule_range_labels[4] = {};
static lv_obj_t *schedule_editor = nullptr;
static lv_obj_t *schedule_temperature_slider = nullptr;
static lv_obj_t *schedule_start_hour_roller = nullptr;
static lv_obj_t *schedule_start_minute_roller = nullptr;
static lv_obj_t *schedule_end_hour_roller = nullptr;
static lv_obj_t *schedule_end_minute_roller = nullptr;
static lv_obj_t *schedule_editor_temperature_label = nullptr;
static uint8_t editing_schedule_index = 0;
static bool wifi_has_connected_once = false;
static uint8_t network_failure_count = 0;
static void resetC6();
static constexpr uint8_t BRIGHTNESS_MIN = 5;
static constexpr uint8_t BRIGHTNESS_MAX = 100;
static constexpr uint8_t BRIGHTNESS_DEFAULT = 100;
static void brightness_slider_event(lv_event_t *e)
{
    const uint8_t brightness = static_cast<uint8_t>(lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e))));
    if (board != nullptr && board->getBacklight() != nullptr) {
        board->getBacklight()->setBrightness(brightness);
    }
    brightness_preferences.putUChar("level", brightness);
    lv_label_set_text_fmt(brightness_label, "Screen brightness: %u%%", brightness);
}
static void update_wifi_diagnostics_locked()
{
    const bool connected = WiFi.status() == WL_CONNECTED;
    lv_label_set_text(wifi_state_label, connected ? "Status: Connected" : "Status: Reconnecting...");
    lv_label_set_text_fmt(wifi_ssid_label, "SSID: %s", connected ? WiFi.SSID().c_str() : WIFI_SSID);
    if (connected) {
        const int32_t rssi = WiFi.RSSI();
        const char *quality = rssi >= -60 ? "excellent" : (rssi >= -70 ? "good" : (rssi >= -80 ? "fair" : "weak"));
        lv_label_set_text_fmt(wifi_signal_label, "Signal: %ld dBm (%s)", static_cast<long>(rssi), quality);
        lv_label_set_text_fmt(wifi_ip_label, "IP address: %s", WiFi.localIP().toString().c_str());
    } else {
        lv_label_set_text(wifi_signal_label, "Signal: unavailable");
        lv_label_set_text(wifi_ip_label, "IP address: unavailable");
    }
}
static bool ensure_wifi_connected()
{
    if (WiFi.status() == WL_CONNECTED) {
        wifi_has_connected_once = true;
        return true;
    }
    if (wifi_has_connected_once) {
        resetC6();
    }
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    for (uint8_t attempt = 0; attempt < 20; ++attempt) {
        if (lvgl_port_lock(-1)) {
            lv_label_set_text(status_label, "Wi-Fi reconnecting...");
            update_wifi_diagnostics_locked();
            lvgl_port_unlock();
        }
        if (WiFi.status() == WL_CONNECTED) {
            wifi_has_connected_once = true;
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}
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
static lv_obj_t *create_gauge(lv_obj_t *parent, const char *title, const char *unit, int32_t min_value, int32_t max_value, lv_color_t color, lv_obj_t **arc_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, 340);
    lv_obj_t *title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0x334155), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *arc = lv_arc_create(card);
    lv_obj_set_size(arc, 230, 230);
    lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 38);
    lv_arc_set_range(arc, min_value, max_value);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_value(arc, min_value);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1A263B), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    *arc_out = arc;
    lv_obj_t *value_label = lv_label_create(card);
    lv_label_set_text(value_label, "--.-");
    lv_obj_set_style_text_color(value_label, lv_color_hex(0x0F172A), 0);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_42, 0);
    lv_obj_align(value_label, LV_ALIGN_TOP_MID, 0, 118);
    lv_obj_t *unit_label = lv_label_create(card);
    lv_label_set_text(unit_label, unit);
    lv_obj_set_style_text_color(unit_label, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(unit_label, &lv_font_montserrat_24, 0);
    lv_obj_align(unit_label, LV_ALIGN_TOP_MID, 0, 175);
    return value_label;
}
static void update_gauge(lv_obj_t *arc, lv_obj_t *value_label, const String &value, int32_t min_value, int32_t max_value)
{
    const float numeric_value = value.toFloat();
    int32_t arc_value = static_cast<int32_t>(numeric_value);
    if (arc_value < min_value) arc_value = min_value;
    if (arc_value > max_value) arc_value = max_value;
    lv_arc_set_value(arc, arc_value);
    lv_label_set_text(value_label, value.c_str());
}
static void page_title(lv_obj_t *page, const char *title)
{
    lv_obj_t *label = lv_label_create(page);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_30, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 18);
}
static bool thermostat_post_json(const char *endpoint, const String &json_body)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("Thermostat POST %s failed: Wi-Fi not connected\n", endpoint);
        return false;
    }
    HTTPClient http;
    const String url = String(THERMOSTAT_BASE_URL) + endpoint;
    if (!http.begin(url)) {
        Serial.printf("Thermostat POST %s failed: http.begin()\n", endpoint);
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    Serial.printf("Thermostat POST %s: %s\n", endpoint, json_body.c_str());
    const int response_code = http.POST(json_body);
    Serial.printf("Thermostat POST %s -> HTTP %d\n", endpoint, response_code);
    if (response_code > 0) {
        const String response = http.getString();
        Serial.printf("Response: %s\n", response.c_str());
    } else {
        Serial.printf("HTTP error: %s\n", http.errorToString(response_code).c_str());
    }
    http.end();
    return response_code >= 200 && response_code < 300;
}
static void thermostat_mode_event(lv_event_t *e)
{
    const char *mode = static_cast<const char *>(lv_event_get_user_data(e));
    const String body = String("{\"mode\":\"") + mode + "\"}";
    thermostat_post_json("/api/mode", body);
    // Do not commit the mode locally. The next GET /api/state
    // will display the authoritative state returned by Raspberry/Python.
}
static void manual_setpoint_event(lv_event_t *e)
{
    lv_obj_t *slider = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const int32_t setpoint = lv_slider_get_value(slider);
    // While dragging, update only the local label for immediate feedback.
    lv_label_set_text_fmt(
        manual_setpoint_label,
        "Manual setpoint: %ld °C",
        static_cast<long>(setpoint)
    );
    // Send only when the user releases the slider.
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        const String body = String("{\"temp\":") + String(setpoint) + "}";
        thermostat_post_json("/api/manual_temp", body);
    }
}
static lv_obj_t *create_mode_button(lv_obj_t *parent, const char *text, int x, bool active)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, 145, 54);
    lv_obj_set_pos(button, x, 402);
    lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, active ? lv_color_hex(0x287BFF) : lv_color_hex(0x17253A), LV_PART_MAIN);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, thermostat_mode_event, LV_EVENT_CLICKED, const_cast<char *>(text));
    return button;
}
static void refresh_schedule_row(uint8_t index)
{
    const Schedule &schedule = schedules[index];
    lv_label_set_text_fmt(schedule_temperature_labels[index], "%.1f °C", schedule.temperature);
    lv_label_set_text_fmt(schedule_range_labels[index], "%02u:%02u - %02u:%02u", schedule.start_hour, schedule.start_minute, schedule.end_hour, schedule.end_minute);
}
static void save_schedule(uint8_t index)
{
    const Schedule &schedule = schedules[index];
    char key[8];
    snprintf(key, sizeof(key), "s%ut", index); brightness_preferences.putUChar(key, schedule.temperature);
    snprintf(key, sizeof(key), "s%ush", index); brightness_preferences.putUChar(key, schedule.start_hour);
    snprintf(key, sizeof(key), "s%usm", index); brightness_preferences.putUChar(key, schedule.start_minute);
    snprintf(key, sizeof(key), "s%ueh", index); brightness_preferences.putUChar(key, schedule.end_hour);
    snprintf(key, sizeof(key), "s%uem", index); brightness_preferences.putUChar(key, schedule.end_minute);
}
static void load_schedules()
{
    for (uint8_t index = 0; index < 4; ++index) {
        Schedule &schedule = schedules[index];
        char key[8];
        snprintf(key, sizeof(key), "s%ut", index); schedule.temperature = brightness_preferences.getUChar(key, schedule.temperature);
        snprintf(key, sizeof(key), "s%ush", index); schedule.start_hour = brightness_preferences.getUChar(key, schedule.start_hour);
        snprintf(key, sizeof(key), "s%usm", index); schedule.start_minute = brightness_preferences.getUChar(key, schedule.start_minute);
        snprintf(key, sizeof(key), "s%ueh", index); schedule.end_hour = brightness_preferences.getUChar(key, schedule.end_hour);
        snprintf(key, sizeof(key), "s%uem", index); schedule.end_minute = brightness_preferences.getUChar(key, schedule.end_minute);
    }
}
static void schedule_temperature_event(lv_event_t *e)
{
    const int32_t temperature = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
    lv_label_set_text_fmt(schedule_editor_temperature_label, "Temperature: %ld °C", static_cast<long>(temperature));
}
static void schedule_editor_close_event(lv_event_t *e)
{
    (void)e;
    lv_obj_delete(schedule_editor);
    schedule_editor = nullptr;
}
static void schedule_editor_save_event(lv_event_t *e)
{
    (void)e;
    const uint8_t index = editing_schedule_index;
    const uint8_t next_index = (index + 1) % 4;
    const float temperature =
        static_cast<float>(lv_slider_get_value(schedule_temperature_slider));
    const uint8_t start_hour =
        static_cast<uint8_t>(lv_roller_get_selected(schedule_start_hour_roller));
    const uint8_t start_minute =
        static_cast<uint8_t>(lv_roller_get_selected(schedule_start_minute_roller) * 15);
    const uint8_t end_hour =
        static_cast<uint8_t>(lv_roller_get_selected(schedule_end_hour_roller));
    const uint8_t end_minute =
        static_cast<uint8_t>(lv_roller_get_selected(schedule_end_minute_roller) * 15);
    char start_text[6];
    char end_text[6];
    snprintf(start_text, sizeof(start_text), "%02u:%02u", start_hour, start_minute);
    snprintf(end_text, sizeof(end_text), "%02u:%02u", end_hour, end_minute);
    // Update the selected interval: its Start and Temperature.
    const String body =
        String("{\"index\":") + String(index) +
        ",\"start\":\"" + String(start_text) + "\"" +
        ",\"temp\":" + String(temperature, 1) + "}";
    const bool current_ok = thermostat_post_json("/api/schedule", body);
    // Consecutive intervals: the End of this interval is the Start of the next one.
    // Preserve the next interval's current temperature.
    const String next_body =
        String("{\"index\":") + String(next_index) +
        ",\"start\":\"" + String(end_text) + "\"" +
        ",\"temp\":" + String(schedules[next_index].temperature, 1) + "}";
    const bool next_ok = thermostat_post_json("/api/schedule", next_body);
    Serial.printf(
        "Schedule save index %u: current=%s next=%s\n",
        index,
        current_ok ? "OK" : "FAILED",
        next_ok ? "OK" : "FAILED"
    );
    // Immediate visual feedback only after both POSTs succeeded.
    // Raspberry/Python remains authoritative: the periodic GET /api/state
    // will still verify and, if necessary, correct these displayed values.
    if (current_ok && next_ok) {
        schedules[index].temperature = temperature;
        schedules[index].start_hour = start_hour;
        schedules[index].start_minute = start_minute;
        schedules[index].end_hour = end_hour;
        schedules[index].end_minute = end_minute;
        schedules[next_index].start_hour = end_hour;
        schedules[next_index].start_minute = end_minute;
        refresh_schedule_row(index);
        refresh_schedule_row(next_index);
    }
    lv_obj_delete(schedule_editor);
    schedule_editor = nullptr;
}
static lv_obj_t *create_time_roller(lv_obj_t *parent, const char *options, int x, int y, uint8_t selected)
{
    lv_obj_t *roller = lv_roller_create(parent);
    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_roller_set_selected(roller, selected, LV_ANIM_OFF);
    lv_obj_set_size(roller, 100, 112);
    lv_obj_set_pos(roller, x, y);
    return roller;
}
static void show_schedule_editor(uint8_t index)
{
    editing_schedule_index = index;
    const Schedule &schedule = schedules[index];
    schedule_editor = lv_obj_create(lv_layer_top());
    lv_obj_set_size(schedule_editor, 690, 440);
    lv_obj_center(schedule_editor);
    lv_obj_set_style_bg_color(schedule_editor, lv_color_hex(0x17253A), LV_PART_MAIN);
    lv_obj_set_style_border_color(schedule_editor, lv_color_hex(0x287BFF), LV_PART_MAIN);
    lv_obj_set_style_border_width(schedule_editor, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(schedule_editor, 16, LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(schedule_editor);
    lv_label_set_text_fmt(title, "Edit %s schedule", schedule.name);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE8EDF5), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_30, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
    schedule_editor_temperature_label = lv_label_create(schedule_editor);
    lv_label_set_text_fmt(schedule_editor_temperature_label, "Temperature: %.1f °C", schedule.temperature);
    lv_obj_set_style_text_color(schedule_editor_temperature_label, lv_color_hex(0xD5DCE8), 0);
    lv_obj_set_style_text_font(schedule_editor_temperature_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(schedule_editor_temperature_label, 38, 82);
    schedule_temperature_slider = lv_slider_create(schedule_editor);
    lv_slider_set_range(schedule_temperature_slider, 10, 30);
    lv_slider_set_value(schedule_temperature_slider, schedule.temperature, LV_ANIM_OFF);
    lv_obj_set_width(schedule_temperature_slider, 580);
    lv_obj_set_pos(schedule_temperature_slider, 38, 120);
    lv_obj_add_event_cb(schedule_temperature_slider, schedule_temperature_event, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *start_label = lv_label_create(schedule_editor);
    lv_label_set_text(start_label, "Start");
    lv_obj_set_style_text_color(start_label, lv_color_hex(0xD5DCE8), 0);
    lv_obj_set_pos(start_label, 165, 172);
    lv_obj_t *end_label = lv_label_create(schedule_editor);
    lv_label_set_text(end_label, "End");
    lv_obj_set_style_text_color(end_label, lv_color_hex(0xD5DCE8), 0);
    lv_obj_set_pos(end_label, 405, 172);
    schedule_start_hour_roller = create_time_roller(schedule_editor, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", 95, 200, schedule.start_hour);
    schedule_start_minute_roller = create_time_roller(schedule_editor, "00\n15\n30\n45", 215, 200, schedule.start_minute / 15);
    schedule_end_hour_roller = create_time_roller(schedule_editor, "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23", 335, 200, schedule.end_hour);
       schedule_end_minute_roller = create_time_roller(schedule_editor, "00\n15\n30\n45", 455, 200, schedule.end_minute / 15);
       lv_obj_t *cancel = lv_button_create(schedule_editor);
    lv_obj_set_size(cancel, 180, 56);
    lv_obj_set_pos(cancel, 105, 360);
    lv_obj_t *cancel_label = lv_label_create(cancel);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);
    lv_obj_add_event_cb(cancel, schedule_editor_close_event, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *save = lv_button_create(schedule_editor);
    lv_obj_set_size(save, 180, 56);
    lv_obj_set_pos(save, 405, 360);
    lv_obj_set_style_bg_color(save, lv_color_hex(0x287BFF), LV_PART_MAIN);
    lv_obj_t *save_label = lv_label_create(save);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);
    lv_obj_add_event_cb(save, schedule_editor_save_event, LV_EVENT_CLICKED, nullptr);
}
static void schedule_row_event(lv_event_t *e)
{
    const uint8_t index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    show_schedule_editor(index);
}
static void create_schedule_row(lv_obj_t *parent, uint8_t index, int y)
{
    const Schedule &schedule = schedules[index];
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_size(row, 505, 54);
    lv_obj_set_pos(row, 440, y);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x101E31), LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(row, schedule_row_event, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));
    lv_obj_t *name_label = lv_label_create(row);
    lv_label_set_text(name_label, schedule.name);
    lv_obj_set_style_text_color(name_label, lv_color_hex(0xD5DCE8), 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_24, 0);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 0, 0);
    schedule_temperature_labels[index] = lv_label_create(row);
    lv_obj_set_style_text_color(schedule_temperature_labels[index], lv_color_hex(0xD5DCE8), 0);
    lv_obj_set_style_text_font(schedule_temperature_labels[index], &lv_font_montserrat_24, 0);
    lv_obj_align(schedule_temperature_labels[index], LV_ALIGN_CENTER, 35, 0);
    schedule_range_labels[index] = lv_label_create(row);
    lv_obj_set_style_text_color(schedule_range_labels[index], lv_color_hex(0xD5DCE8), 0);
    lv_obj_align(schedule_range_labels[index], LV_ALIGN_RIGHT_MID, 0, 0);
    refresh_schedule_row(index);
}
static void create_sensor_ui(void)
{
    if (lvgl_port_lock(-1) != true) {
        MAIN_ERROR("LVGL lock failed");
        return;
    }
    brightness_preferences.begin("display", false);
    load_schedules();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x07101F), LV_PART_MAIN);
    lv_obj_t *pages = lv_tileview_create(scr);
    lv_obj_set_size(pages, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollbar_mode(pages, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *garden_page = lv_tileview_add_tile(pages, 0, 0, LV_DIR_HOR);
    lv_obj_t *home_page = lv_tileview_add_tile(pages, 1, 0, LV_DIR_HOR);
    lv_obj_t *settings_page = lv_tileview_add_tile(pages, 2, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(garden_page, lv_color_hex(0x07101F), LV_PART_MAIN);
    lv_obj_set_style_bg_color(home_page, lv_color_hex(0x07101F), LV_PART_MAIN);
    lv_obj_set_style_bg_color(settings_page, lv_color_hex(0x07101F), LV_PART_MAIN);
    page_title(garden_page, "GARDEN");
    garden_temperature_label = create_gauge(garden_page, "TEMPERATURE", "°C", -10, 50, lv_color_hex(0x4AC6FF), &garden_temperature_arc);
    humidity_label = create_gauge(garden_page, "HUMIDITY", "%", 0, 100, lv_color_hex(0x42D8A6), &humidity_arc);
    lv_obj_set_pos(lv_obj_get_parent(garden_temperature_label), 190, 100);
    lv_obj_set_pos(lv_obj_get_parent(humidity_label), 535, 100);
    status_label = lv_label_create(garden_page);
    lv_label_set_text(status_label, "Connecting to Wi-Fi...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x475569), 0);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    page_title(home_page, "HOME THERMOSTAT");
    temperature_label = create_gauge(home_page, "CURRENT TEMPERATURE", "°C", -10, 50, lv_color_hex(0x3784FF), &temperature_arc);
    lv_obj_set_pos(lv_obj_get_parent(temperature_label), 60, 78);
    boiler_flame_gif = lv_gif_create(home_page);
    lv_gif_set_src(boiler_flame_gif, &ani_flame_gif_dsc);
    lv_obj_set_size(boiler_flame_gif, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(boiler_flame_gif, 145, 390);
    lv_obj_add_flag(boiler_flame_gif, LV_OBJ_FLAG_HIDDEN);
    create_schedule_row(home_page, 0, 92);
    create_schedule_row(home_page, 1, 158);
    create_schedule_row(home_page, 2, 224);
    create_schedule_row(home_page, 3, 290);
    thermostat_mode_label = lv_label_create(home_page);
    lv_label_set_text(thermostat_mode_label, "Mode: Thermostat");
    lv_obj_set_style_text_color(thermostat_mode_label, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(thermostat_mode_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(thermostat_mode_label, 500, 370);
    thermostat_buttons[0] = create_mode_button(home_page, "Off", 500, false);
    thermostat_buttons[1] = create_mode_button(home_page, "Thermostat", 660, true);
    thermostat_buttons[2] = create_mode_button(home_page, "Manual", 820, false);
    manual_setpoint_label = lv_label_create(home_page);
    lv_label_set_text(manual_setpoint_label, "Manual setpoint: 21 °C");
    lv_obj_set_style_text_color(manual_setpoint_label, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(manual_setpoint_label, &lv_font_montserrat_24, 0);
    lv_obj_set_pos(manual_setpoint_label, 500, 478);
    manual_setpoint_slider = lv_slider_create(home_page);
   lv_obj_t *setpoint_slider = manual_setpoint_slider;
    lv_slider_set_range(setpoint_slider, 10, 30);
    lv_slider_set_value(setpoint_slider, 21, LV_ANIM_OFF);
    lv_obj_set_width(setpoint_slider, 390);
    lv_obj_set_pos(setpoint_slider, 500, 520);
    lv_obj_add_event_cb(setpoint_slider, manual_setpoint_event, LV_EVENT_VALUE_CHANGED, nullptr);
   lv_obj_add_event_cb(setpoint_slider, manual_setpoint_event, LV_EVENT_RELEASED, nullptr);
    page_title(settings_page, "SETTINGS");
    lv_obj_t *wifi_title = lv_label_create(settings_page);
    lv_label_set_text(wifi_title, "Wi-Fi diagnostics");
    lv_obj_set_style_text_color(wifi_title, lv_color_hex(0x1E293B), 0);
    lv_obj_set_style_text_font(wifi_title, &lv_font_montserrat_24, 0);
    lv_obj_align(wifi_title, LV_ALIGN_TOP_MID, 0, 88);
    wifi_state_label = lv_label_create(settings_page);
    wifi_ssid_label = lv_label_create(settings_page);
    wifi_signal_label = lv_label_create(settings_page);
    wifi_ip_label = lv_label_create(settings_page);
    lv_obj_t *wifi_labels[] = {wifi_state_label, wifi_ssid_label, wifi_signal_label, wifi_ip_label};
    for (uint8_t i = 0; i < 4; ++i) {
        lv_obj_set_style_text_color(wifi_labels[i], lv_color_hex(0x475569), 0);
        lv_obj_set_style_text_font(wifi_labels[i], &lv_font_montserrat_24, 0);
        lv_obj_set_pos(wifi_labels[i], 230, 132 + i * 36);
    }
    update_wifi_diagnostics_locked();
    const uint8_t brightness = brightness_preferences.getUChar("level", BRIGHTNESS_DEFAULT);
    brightness_label = lv_label_create(settings_page);
    lv_obj_set_style_text_color(brightness_label, lv_color_hex(0x475569), 0);
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_24, 0);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 350);
    lv_obj_t *brightness_slider = lv_slider_create(settings_page);
    lv_slider_set_range(brightness_slider, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    lv_slider_set_value(brightness_slider, brightness, LV_ANIM_OFF);
    lv_obj_set_width(brightness_slider, 540);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 400);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_event, LV_EVENT_VALUE_CHANGED, nullptr);
    if (board != nullptr && board->getBacklight() != nullptr) {
        board->getBacklight()->setBrightness(brightness);
    }
    lv_label_set_text_fmt(brightness_label, "Screen brightness: %u%%", brightness);
    lvgl_port_unlock();
}
static bool read_thermostat_state()
{
    HTTPClient http;
    const String url = String(THERMOSTAT_BASE_URL) + "/api/state";
    if (!http.begin(url)) {
        return false;
    }
    const int response_code = http.GET();
    if (response_code != HTTP_CODE_OK) {
        http.end();
        return false;
    }
    const String payload = http.getString();
    http.end();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.printf("Thermostat JSON error: %s\n", error.c_str());
        return false;
    }
    const char *mode = doc["mode"] | "Thermostat";
    const float room_temp = doc["room_temp"] | 0.0f;
    const float setpoint = doc["setpoint"] | 20.0f;
    const bool heating = doc["heating"] | false;
    JsonArray schedule = doc["schedule"].as<JsonArray>();
    if (schedule.size() != 4) {
        return false;
    }
    if (lvgl_port_lock(-1)) {
        update_gauge(temperature_arc, temperature_label, String(room_temp, 1), -10, 50);
        thermostat_mode = mode;
        lv_label_set_text_fmt(thermostat_mode_label, "Mode: %s", mode);
        for (uint8_t i = 0; i < 3; ++i) {
            const char *button_mode = (i == 0) ? "Off" : (i == 1) ? "Thermostat" : "Manual";
            lv_obj_set_style_bg_color(
                thermostat_buttons[i],
                strcmp(mode, button_mode) == 0 ? lv_color_hex(0x287BFF) : lv_color_hex(0x17253A),
                LV_PART_MAIN);
        }
        lv_label_set_text_fmt(manual_setpoint_label, "Manual setpoint: %.1f °C", setpoint);
        if (manual_setpoint_slider != nullptr) {
            lv_slider_set_value(manual_setpoint_slider, static_cast<int32_t>(setpoint), LV_ANIM_OFF);
        }
        for (uint8_t i = 0; i < 4; ++i) {
            const char *start = schedule[i]["start"] | "00:00";
            const float temp = schedule[i]["temp"] | 20.0f;
            int sh = 0, sm = 0;
            sscanf(start, "%d:%d", &sh, &sm);
            schedules[i].temperature = temp;
            schedules[i].start_hour = sh;
            schedules[i].start_minute = sm;
        }
        // End of each interval is the start of the next; last wraps to first.
        for (uint8_t i = 0; i < 4; ++i) {
            const uint8_t next = (i + 1) % 4;
            schedules[i].end_hour = schedules[next].start_hour;
            schedules[i].end_minute = schedules[next].start_minute;
            refresh_schedule_row(i);
        }
        if (boiler_flame_gif != nullptr) {
            if (heating) {
                lv_obj_remove_flag(boiler_flame_gif, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(boiler_flame_gif, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (status_label != nullptr) {
            lv_label_set_text(status_label, heating ? "Boiler ON - Raspberry master" : "Boiler OFF - Raspberry master");
        }
        lvgl_port_unlock();
    }
    return true;
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
    while (true) {
        if (!ensure_wifi_connected()) {
            if (lvgl_port_lock(-1)) {
                lv_label_set_text(status_label, "Wi-Fi unavailable; retrying shortly");
                update_wifi_diagnostics_locked();
                lvgl_port_unlock();
            }
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }
        String temperature;
        String garden_temperature;
        String humidity;
        const bool thermostat_ok = read_thermostat_state();
        const bool garden_temperature_ok = read_home_assistant_state(HA_GARDEN_TEMPERATURE_ENTITY, garden_temperature);
        const bool humidity_ok = read_home_assistant_state(HA_HUMIDITY_ENTITY, humidity);
       if (!thermostat_ok && !garden_temperature_ok && !humidity_ok) {
           network_failure_count++;
           Serial.printf("Network watchdog: %u consecutive complete failures\n", network_failure_count);
           if (network_failure_count >= 3) {
               Serial.println("Network watchdog: resetting ESP32-C6");
               network_failure_count = 0;
               wifi_has_connected_once = false;
               WiFi.disconnect();
               resetC6();
               vTaskDelay(pdMS_TO_TICKS(1000));
               WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
               vTaskDelay(pdMS_TO_TICKS(5000));
               continue;
           }
       } else {
           network_failure_count = 0;
       }
        if (lvgl_port_lock(-1)) {
            if (garden_temperature_ok) {
                update_gauge(garden_temperature_arc, garden_temperature_label, garden_temperature, -10, 50);
            }
            if (humidity_ok) {
                update_gauge(humidity_arc, humidity_label, humidity, 0, 100);
            }
            if (!thermostat_ok) lv_label_set_text(status_label, "Raspberry thermostat request failed");
            update_wifi_diagnostics_locked();
            lvgl_port_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(THERMOSTAT_REFRESH_MS));
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
    board = new Board();
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
