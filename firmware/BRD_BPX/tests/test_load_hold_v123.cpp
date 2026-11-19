/*
    檔案：BRD_BPX/tests/test_load_hold_v123.cpp
    [V1.2.3 新增] 使用真實 LOAD/RPM 量測 + BBP Session 驗證五階段流程。
    [驗證] 30% 等號連續確認後結束、HOLD 先停 ISR、3 秒不檢查 LOAD、不清 MAX；
           HOLD 才封存 MAX/曲線；結束後 WAIT LOAD -> LOW -> HIGH 才歸零。
*/
#include <cassert>
#include <cstdio>
#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include "brd_config.h"
#include "brd_io.h"
#include "brd_measurement.h"
#include "brd_settings.h"
#include "brd_bbp_session.h"

uint64_t test_clock_us = 100000ULL;
void (*test_interrupts[32])(void) = {};
TestGPIO GPIO = {};
static brd_bbp::Session session;
static unsigned finish_calls = 0U;
static int test_led_level = HIGH;
static bool test_led_write_fails = false;

static esp_err_t test_gpio_set_level(gpio_num_t pin, uint32_t level) {
    assert(pin == STATUS_LED_GPIO);
    if (test_led_write_fails) { return 1; }
    test_led_level = static_cast<int>(level);
    return ESP_OK;
}

#define gpio_set_level test_gpio_set_level
#include "../brd_io.cpp"
#undef gpio_set_level

const brd_settings::Values &brd_settings_get() {
    static const brd_settings::Values value = {BRD_MEASUREMENT_MODE_LOAD, 0U, 30U, 2000U, BATTERY_CAPACITY_200_MAH, OLED_MAX_HOLD_MS};
    return value;
}
void brd_bbp_capture_reset() { session.reset_capture(); }
void brd_bbp_capture_period(uint32_t period, uint32_t time, bool record) {
    session.period(period, time, record);
}
void brd_bbp_capture_launch(uint32_t time) { session.launched(time); }
void brd_bbp_capture_finish() {
    /* [V1.2.3 驗證] 進入 HOLD 並禁止 ISR 後，才能凍結結果。 */
    assert(brd_measurement_get_display().hold_active);
    ++finish_calls;
    session.finish();
}
void brd_bbp_capture_abort() { session.abort(); }

static void set_time_ms(uint64_t now_ms) {
    test_clock_us = now_ms * 1000ULL;
}
static void test_ready_led() {
    set_time_ms(1000U);
    brd_io_begin();
    assert(test_led_level == HIGH);
    brd_io_update_loaded_ready_led(true);
    assert(test_led_level == LOW);
    set_time_ms(1500U);
    brd_io_update_loaded_ready_led(true);
    assert(test_led_level == LOW);
    set_time_ms(6000U);
    brd_io_update_loaded_ready_led(true);
    assert(test_led_level == LOW);
    set_time_ms(60000U);
    brd_io_update_loaded_ready_led(true);
    assert(test_led_level == LOW);
    set_time_ms(1000ULL + (1ULL << 32U));
    brd_io_update_loaded_ready_led(true);
    assert(test_led_level == LOW);

    test_led_write_fails = true;
    brd_io_update_loaded_ready_led(false);
    assert(test_led_level == LOW);
    test_led_write_fails = false;
    brd_io_update_loaded_ready_led(false);
    assert(test_led_level == HIGH);
    test_led_write_fails = true;
    brd_io_update_loaded_ready_led(true);
    assert(test_led_level == HIGH);
    test_led_write_fails = false;
    brd_io_update_loaded_ready_led(true);
    assert(test_led_level == LOW);
    brd_io_update_loaded_ready_led(false);
    assert(test_led_level == HIGH);

    test_clock_us = 100000ULL;
    brd_io_begin();
}
static void update_measurement() {
    brd_measurement_update();
    brd_io_update_loaded_ready_led(brd_measurement_is_loaded_ready());
}
static void tick(uint32_t us) {
    test_clock_us += us;
    update_measurement();
}
static void load(int level, uint32_t us) {
    test_clock_us += us;
    GPIO.in.val = (GPIO.in.val & ~(1U << LOAD_IR_GPIO)) |
                  (static_cast<uint32_t>(level) << LOAD_IR_GPIO);
    if (test_interrupts[LOAD_IR_GPIO]) { test_interrupts[LOAD_IR_GPIO](); }
    update_measurement();
}
static void rpm(uint32_t us) {
    test_clock_us += us;
    GPIO.in.val |= 1U << RPM_IR_GPIO;
    if (test_interrupts[RPM_IR_GPIO]) { test_interrupts[RPM_IR_GPIO](); }
    update_measurement();
}
static void shot_10000_then_3000() {
    const uint32_t previous_total = session.state().total;
    rpm(100U);
    assert(brd_measurement_get_display().loaded);
    assert(brd_measurement_is_loaded_ready());
    assert(test_led_level == LOW);
    // Falling-to-Falling 週期建立 10000 RPM。
    for (unsigned i = 0U; i < 5U; ++i) { rpm(6000U); }
    assert(brd_measurement_get_display().loaded);
    assert(brd_measurement_is_loaded_ready());
    assert(test_led_level == LOW);
    assert(!brd_measurement_get_display().show_max);
    assert(!session.pending());
    assert(session.state().total == previous_total);
    // 3000 == 10000 x 30%。
    rpm(20000U);
    /* [修改] 3000 == MAX*30%，首個低值直接進 HOLD，不需多筆確認。 */
    assert(brd_measurement_get_display().hold_active);
    for (unsigned i = 0U; i < 3U; ++i) { rpm(20000U); }
    assert(brd_measurement_get_display().show_max);
    assert(brd_measurement_get_display().value == 10000U);
    assert(brd_measurement_get_display().hold_active);
    assert(!brd_measurement_is_loaded_ready());
    assert(test_led_level == HIGH);
}
int main() {
    static_assert(LOAD_MAX_HOLD_MS == 2500UL, "LOAD HOLD must match original V1.2 2.5 seconds");
    test_ready_led();
    brd_measurement_begin();
    assert(!brd_measurement_is_loaded_ready());
    assert(!brd_measurement_get_display().loaded);
    assert(!brd_measurement_get_display().show_max);
    rpm(100U); rpm(6000U); // WAIT LOAD ignores RPM
    assert(!brd_measurement_get_display().show_max);
    load(HIGH, 1000U);
    tick(LOAD_IR_DEBOUNCE_US-1U);
    assert(!brd_measurement_get_display().loaded);
    tick(1U);
    assert(brd_measurement_get_display().loaded);
    assert(brd_measurement_is_loaded_ready());
    assert(test_led_level == LOW);
    load(LOW, 100U);
    tick(LOAD_IR_DEBOUNCE_US);
    assert(!brd_measurement_is_loaded_ready());
    assert(test_led_level == HIGH);
    load(HIGH, 100U);
    tick(LOAD_IR_DEBOUNCE_US);
    assert(brd_measurement_is_loaded_ready());
    assert(test_led_level == LOW);
    rpm(100U);
    tick(1000000U);
    assert(brd_measurement_get_display().loaded);
    assert(brd_measurement_is_loaded_ready());
    assert(test_led_level == LOW);
    tick((PRELAUNCH_IDLE_RESET_MS - 1000U) * 1000U);
    assert(brd_measurement_is_loaded_ready());
    assert(test_led_level == LOW);
    shot_10000_then_3000();
    assert(finish_calls == 1U);
    assert(session.pending());
    brd_display_t frame = brd_measurement_get_display();
    brd_measurement_max_frame_presented(frame.generation);
    /* [驗證] HOLD 期間的 LOAD LOW/HIGH 彈跳及 RPM 不可清 MAX/增加歷史。 */
    const uint32_t load_changes_before_hold_bounce =
        brd_measurement_get_diagnostics().load_stable_transitions;
    load(LOW, 10000U);
    load(HIGH, 10000U);
    rpm(5000U); rpm(6000U); rpm(20000U);
    assert(brd_measurement_get_diagnostics().load_stable_transitions ==
           load_changes_before_hold_bounce);
    tick(600000U);
    session.update(micros(), BBP_PUBLISH_MIN_DELAY_US); // HOLD 內才發布
    assert(session.state().total == 1U);
    assert(session.state().lifetime_max == 10000U);
    assert(session.state().curve[0] != 0U);
    assert(finish_calls == 1U);
    assert(brd_measurement_get_display().hold_active);
    /* [驗證] HOLD 鎖定時長相對 MAX 畫面完成，不是從 IR 事件起算。 */
    tick(1800000U); // [V1.4.2 修改] 約 2.45 秒，仍在 HOLD
    assert(brd_measurement_get_display().hold_active);
    tick(100000U); // [V1.4.2 修改] 超過 2.5 秒，HOLD 結束
    assert(!brd_measurement_get_display().hold_active);
    assert(!brd_measurement_get_display().loaded); // WAIT LOAD despite physical HIGH
    assert(brd_measurement_get_display().show_max);
    assert(brd_measurement_get_display().value == 10000U);
    rpm(100U); rpm(6000U); rpm(20000U);
    assert(finish_calls == 1U);
    assert(brd_measurement_get_display().show_max);
    load(LOW, 100U); tick(LOAD_IR_DEBOUNCE_US-1000U);
    load(HIGH, 100U); tick(LOAD_IR_DEBOUNCE_US+1000U);
    assert(!brd_measurement_get_display().loaded);
    assert(brd_measurement_get_display().show_max);
    load(LOW, 100U); tick(LOAD_IR_DEBOUNCE_US);
    assert(!brd_measurement_get_display().loaded);
    assert(brd_measurement_get_display().show_max);
    load(HIGH, 100U); tick(LOAD_IR_DEBOUNCE_US);
    assert(brd_measurement_get_display().loaded);
    assert(brd_measurement_is_loaded_ready());
    assert(test_led_level == LOW);
    assert(!brd_measurement_get_display().show_max); // only real LOADED clears old MAX
    assert(brd_measurement_get_display().value == 0U);
    shot_10000_then_3000();
    assert(finish_calls == 2U);
    assert(session.state().total == 1U); // second record is not published until HOLD
    assert(brd_measurement_get_display().hold_active);
    tick(600000U);
    session.update(micros(), BBP_PUBLISH_MIN_DELAY_US);
    assert(session.state().total == 2U);
    puts("PASS: LOAD -> SP -> MAX threshold equality -> HOLD 2.5s / BLE record -> WAIT LOAD -> LOADED reset.");
}
