// SPDX-License-Identifier: GPL-3.0-or-later
#include "touch_input.h"

#include "driver/i2c_master.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "ktouch.h"
#include "lvgl.h"
#include "touch_edge_queue.h"
#include "touch_read_health.h"

static const char* TAG = "touch";
static esp_lcd_touch_handle_t s_touch;
static bool s_available;

// The GT911 keeps only its latest frame, and a UI cycle can run for hundreds
// of milliseconds, so a read taken once per cycle misses the release between
// two fast taps. A sampler task reads the controller every
// TOUCH_SAMPLE_PERIOD_MS and queues each press, move and release; the indev
// read hands them to LVGL one per read with continue_reading set, so every tap
// arrives whole however slow the frame was. The sampler is the only task that
// talks I2C after init.
//
// It outranks the UI task, or it could not run during a long render, and
// costs one short I2C read per period. Its stack is in PSRAM: it never touches
// flash, and the I2C interrupt is not IRAM-safe, so nothing writes that stack
// while the cache is off. Polling alone peaks near 1.9KB of it; the rest is
// room for the log line a failing read prints (vfprintf), and costs no
// internal RAM.
#define TOUCH_SAMPLE_PERIOD_MS 10
#define TOUCH_SAMPLER_STACK_BYTES 4096
#define TOUCH_SAMPLER_PRIO 6
#if CONFIG_I2C_ISR_IRAM_SAFE
#error "an IRAM-safe I2C interrupt can write the touch sampler's PSRAM stack with the cache off"
#endif

static portMUX_TYPE s_queue_lock = portMUX_INITIALIZER_UNLOCKED;
// Tasks only, never an ISR, and no task runs while the cache is off.
static EXT_RAM_BSS_ATTR touch_edge_queue s_queue;
static uint8_t s_inject_track = 0x80;

static void queue_push(bool pressed, uint8_t track, int16_t x, int16_t y) {
    taskENTER_CRITICAL(&s_queue_lock);
    touch_edge_queue_push(&s_queue, pressed, track, x, y);
    taskEXIT_CRITICAL(&s_queue_lock);
}

// Console-injected fingers take track ids from 0x80 up, clear of the small ids the GT911 assigns.
static uint8_t next_inject_track(void) {
    s_inject_track = (uint8_t)(0x80 | (s_inject_track + 1));
    return s_inject_track;
}

void touch_input_inject_tap(int x, int y) {
    taskENTER_CRITICAL(&s_queue_lock);
    touch_edge_queue_push(&s_queue, true, next_inject_track(), (int16_t)x, (int16_t)y);
    touch_edge_queue_push(&s_queue, false, 0, 0, 0);
    taskEXIT_CRITICAL(&s_queue_lock);
}

void touch_input_inject_down(int x, int y) {
    taskENTER_CRITICAL(&s_queue_lock);
    touch_edge_queue_push(&s_queue, true, next_inject_track(), (int16_t)x, (int16_t)y);
    taskEXIT_CRITICAL(&s_queue_lock);
}

void touch_input_inject_up(void) {
    queue_push(false, 0, 0, 0);
}

static void sampler_task(void* arg) {
    (void)arg;
    bool was_pressed = false;
    uint8_t last_track = 0;
    uint16_t last_x = 0;
    uint16_t last_y = 0;
    touch_read_health health;
    touch_read_health_init(&health);
    uint32_t period_ms = TOUCH_SAMPLE_PERIOD_MS;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(period_ms));
        const esp_err_t err = esp_lcd_touch_read_data(s_touch);
        const touch_read_verdict verdict = touch_read_health_record(
            &health, err == ESP_OK, esp_timer_get_time() / 1000, TOUCH_SAMPLE_PERIOD_MS);
        period_ms = verdict.next_period_ms;
        if (verdict.log) {
            ESP_LOGW(TAG, "read failed x%u: %s", (unsigned)verdict.failures, esp_err_to_name(err));
        }
        if (err != ESP_OK) {
            continue;
        }
        // The first point straight from the handle's data, under its lock:
        // esp_lcd_touch_get_data would copy every point and pull in its
        // argument checks for nothing.
        portENTER_CRITICAL(&s_touch->data.lock);
        const bool pressed = s_touch->data.points > 0;
        const esp_lcd_touch_point_data_t point = s_touch->data.coords[0];
        portEXIT_CRITICAL(&s_touch->data.lock);
        // Only changes are queued, so a console-injected finger stays down
        // until the controller itself reports something new.
        if (pressed == was_pressed && (!pressed || (point.track_id == last_track &&
                                                    point.x == last_x && point.y == last_y))) {
            continue;
        }
        was_pressed = pressed;
        last_track = point.track_id;
        last_x = point.x;
        last_y = point.y;
        queue_push(pressed, point.track_id, (int16_t)point.x, (int16_t)point.y);
    }
}

static void indev_read(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    touch_edge edge;
    taskENTER_CRITICAL(&s_queue_lock);
    const bool more = touch_edge_queue_pop(&s_queue, &edge);
    taskEXIT_CRITICAL(&s_queue_lock);
    data->point.x = edge.x;
    data->point.y = edge.y;
    data->state = touch_edge_is_pressed(&edge) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = more;
}

bool touch_input_init(void) {
    s_available = false;
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .scl_io_num = BOARD_TOUCH_I2C_SCL,
        .sda_io_num = BOARD_TOUCH_I2C_SDA,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c master bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 panel io init failed: %s", esp_err_to_name(err));
        i2c_del_master_bus(bus);
        return false;
    }

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = BOARD_TOUCH_PIN_RST,
        .int_gpio_num = BOARD_TOUCH_PIN_IRQ,
    };
    // This is the step that actually talks to the controller (GT911 product-ID
    // read), so a degraded or disconnected touch ribbon fails here rather than
    // at bus creation. It must not be fatal: aborting leaves the board in a
    // reset loop with a perfectly usable display.
    err = esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GT911 probe failed: %s - continuing without touch", esp_err_to_name(err));
        s_touch = NULL;
        esp_lcd_panel_io_del(io);
        i2c_del_master_bus(bus);
        return false;
    }

    // Registration and the sampler start only after a successful probe: the
    // sampler dereferences s_touch unconditionally.
    touch_edge_queue_init(&s_queue);
    // The driver logs every failed read itself; the sampler reports them
    // rate-limited instead (touch_read_health.h).
    esp_log_level_set("GT911", ESP_LOG_NONE);
    if (xTaskCreateWithCaps(sampler_task, "touch", TOUCH_SAMPLER_STACK_BYTES, NULL,
                            TOUCH_SAMPLER_PRIO, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "no sampler task: no touch");
        esp_lcd_touch_del(s_touch);
        s_touch = NULL;
        esp_lcd_panel_io_del(io);
        i2c_del_master_bus(bus);
        return false;
    }
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read);
    s_available = true;
    ESP_LOGI(TAG, "GT911 indev registered");
    return true;
}

unsigned touch_input_dropped_taps(void) {
    taskENTER_CRITICAL(&s_queue_lock);
    const unsigned dropped = s_queue.dropped_taps;
    taskEXIT_CRITICAL(&s_queue_lock);
    return dropped;
}

bool touch_input_available(void) {
    return s_available;
}
