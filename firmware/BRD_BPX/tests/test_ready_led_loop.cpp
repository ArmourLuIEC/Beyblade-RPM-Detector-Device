#include <cassert>
#include <cstdio>
#include <Arduino.h>

uint64_t test_clock_us = 0U;
void (*test_interrupts[32])(void) = {};

#include "../BRD_BPX.ino"

static bool battery_allowed = true;
static bool ota_busy = false;
static bool start_ota_on_update = false;
static bool capture_active = false;
static bool start_capture_on_update = false;
static bool loaded_ready = false;
static bool charging = false;
static uint8_t display_page = BRD_DISPLAY_PAGE_MAIN;
static bool led_ready = false;
static unsigned led_updates = 0U;
static unsigned measurement_updates = 0U;
static unsigned measurement_stops = 0U;
static unsigned ota_updates = 0U;
static unsigned bbp_updates = 0U;
static unsigned settings_ble_updates = 0U;
static unsigned settings_updates = 0U;

void brd_settings_begin() {}
void brd_ble_identity_begin() {}
void brd_io_begin() {}
void brd_battery_begin() {}
void brd_oled_begin() {}
void brd_battery_update() {}
bool brd_battery_measurement_allowed() { return battery_allowed; }
bool brd_battery_restart_required() { return false; }
bool brd_bbp_begin() { return true; }
void brd_bbp_stop() {}
bool brd_bbp_prepare_restart() { return false; }
void brd_bbp_update(bool) { ++bbp_updates; }
void brd_measurement_begin() {}
void brd_measurement_stop() { ++measurement_stops; }
void brd_measurement_update() {
    ++measurement_updates;
    if (start_capture_on_update) {
        capture_active = true;
        start_capture_on_update = false;
    }
}
bool brd_measurement_capture_active() { return capture_active; }
bool brd_measurement_is_loaded_ready() { return loaded_ready; }
bool brd_measurement_storage_allowed() { return true; }
void brd_io_update_loaded_ready_led(bool ready) {
    led_ready = ready;
    ++led_updates;
}
bool brd_io_is_charging() { return charging; }
uint8_t brd_settings_ble_display_page() { return display_page; }
void brd_ota_update() {
    ++ota_updates;
    if (start_ota_on_update) {
        ota_busy = true;
        start_ota_on_update = false;
    }
}
bool brd_ota_is_busy() { return ota_busy; }
bool brd_settings_bluetooth_allowed(bool) { return true; }
void brd_settings_ble_update() { ++settings_ble_updates; }
void brd_settings_update() { ++settings_updates; }
const brd_settings::Manager &brd_settings_model() {
    static const brd_settings::Values defaults = {
        BRD_MEASUREMENT_MODE_LOAD, 0U, 30U, 2000U, BATTERY_CAPACITY_200_MAH, OLED_MAX_HOLD_MS
    };
    static const brd_settings::Manager model(defaults, 60000U);
    return model;
}
void brd_ble_identity_update() {}
void brd_oled_update() {}

int main() {
    loaded_ready = true;
    loop();
    assert(led_updates == 1U && led_ready);

    loaded_ready = false;
    loop();
    assert(!led_ready);

    loaded_ready = true;
    loop();
    assert(led_ready);

    charging = true;
    loop();
    assert(led_ready);

    loaded_ready = false;
    loop();
    assert(!led_ready);
    loaded_ready = true;
    loop();
    assert(led_ready);

    display_page = BRD_DISPLAY_PAGE_IR_TEST;
    loop();
    assert(!led_ready);
    display_page = BRD_DISPLAY_PAGE_MAIN;
    loop();
    assert(led_ready);

    charging = false;
    loop();
    assert(led_ready);

    display_page = BRD_DISPLAY_PAGE_IR_TEST;
    loop();
    assert(!led_ready);
    display_page = BRD_DISPLAY_PAGE_MAIN;
    loop();
    assert(led_ready);

    charging = true;
    start_ota_on_update = true;
    const unsigned before_ota = measurement_updates;
    loop();
    assert(measurement_updates == before_ota + 1U);
    assert(!led_ready);
    loop();
    assert(measurement_updates == before_ota + 1U);
    assert(!led_ready);

    ota_busy = false;
    loop();
    assert(led_ready);

    start_capture_on_update = true;
    const unsigned before_ota_updates = ota_updates;
    const unsigned before_bbp_updates = bbp_updates;
    const unsigned before_settings_ble_updates = settings_ble_updates;
    const unsigned before_settings_updates = settings_updates;
    loop();
    assert(capture_active);
    assert(led_ready);
    assert(ota_updates == before_ota_updates);
    assert(bbp_updates == before_bbp_updates);
    assert(settings_ble_updates == before_settings_ble_updates);
    assert(settings_updates == before_settings_updates);
    display_page = BRD_DISPLAY_PAGE_IR_TEST;
    loop();
    assert(!led_ready);
    display_page = BRD_DISPLAY_PAGE_MAIN;
    loop();
    assert(led_ready);
    loaded_ready = false;
    loop();
    assert(!led_ready);

    battery_allowed = false;
    loop();
    assert(measurement_stops == 1U);
    assert(!led_ready);
    puts("PASS: ready LED input follows measurement, OTA and battery safety.");
}
