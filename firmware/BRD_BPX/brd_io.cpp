/*
    [BRD_BBP 修改] 僅更新檔案路徑註解，原功能保留。
    [V0.12 修改] 更新檔案位置註解，功能不變。
    檔案位置: BRD_BBP/brd_io.cpp
    [V0.10 修正] 充電 DET 使用內部上拉，其餘專案輸入腳不啟用內部上下拉。
    [V0.10 恢復] 充電 DET 讀取，提供 OLED 充電圖示狀態。
    [修改] GPIO8 在 LOADED READY 期間恆亮，HOLD 期間閃爍。
*/
#include <driver/gpio.h>
#include <esp_log.h>

#include "brd_config.h"
#include "brd_io.h"

static bool g_status_led_lit = false;
static bool g_status_led_state_known = false;
static bool g_status_led_error_reported = false;
static brd_status_led_mode_t g_status_led_mode = BRD_STATUS_LED_OFF;
static uint32_t g_hold_led_start_ms = 0U;
static constexpr uint32_t HOLD_LED_BLINK_INTERVAL_MS = 250U;

static void set_status_led(bool lit) {
    const esp_err_t error = gpio_set_level((gpio_num_t)STATUS_LED_GPIO,
                                          lit ? LOW : STATUS_LED_INACTIVE_LEVEL);
    if (error != ESP_OK) {
        g_status_led_state_known = false;
        if (!g_status_led_error_reported) {
            ESP_LOGE("brd_io", "GPIO%d LED write failed: %d",
                     STATUS_LED_GPIO, static_cast<int>(error));
            g_status_led_error_reported = true;
        }
        return;
    }
    g_status_led_lit = lit;
    g_status_led_state_known = true;
    g_status_led_error_reported = false;
}

static void input_without_pull(uint8_t pin) {
    pinMode(pin, INPUT);
    (void)gpio_set_pull_mode((gpio_num_t)pin, GPIO_FLOATING);
}

void brd_io_begin(void) {
    g_status_led_state_known = false;
    g_status_led_error_reported = false;
    g_status_led_mode = BRD_STATUS_LED_OFF;

    input_without_pull(RPM_IR_GPIO);
    input_without_pull(LOAD_IR_GPIO);

    /* [V0.10 恢復] GPIO0 作電池 ADC；初始化時不啟用內部上下拉。 */
    input_without_pull(BATTERY_ADC_GPIO);

    /* [V0.10 修正] GPIO10 使用內部上拉，關閉內部下拉。 */
    pinMode(CHRG_DET_GPIO, CHRG_DET_INPUT_MODE);
    (void)gpio_set_pull_mode((gpio_num_t)CHRG_DET_GPIO, GPIO_PULLUP_ONLY);

    /* [V0.10 新增] I2C 啟動前先清除 SDA/SCL 可能殘留的內部上下拉。 */
    input_without_pull(I2C_SDA_GPIO);
    input_without_pull(I2C_SCL_GPIO);

    /* [V0.10 修改] 先設定輸出鎖存電位，再切換為 OUTPUT，初始化時關閉 LED。 */
    set_status_led(false);
    pinMode(STATUS_LED_GPIO, OUTPUT);
    (void)gpio_set_pull_mode((gpio_num_t)STATUS_LED_GPIO, GPIO_FLOATING);
    set_status_led(false);
}

void brd_io_update_status_led(brd_status_led_mode_t mode) {
    bool lit = mode == BRD_STATUS_LED_READY;
    if (mode == BRD_STATUS_LED_HOLD) {
        const uint32_t now_ms = millis();
        if (g_status_led_mode != BRD_STATUS_LED_HOLD) {
            g_hold_led_start_ms = now_ms;
        }
        lit = (static_cast<uint32_t>(now_ms - g_hold_led_start_ms) /
               HOLD_LED_BLINK_INTERVAL_MS) % 2U == 0U;
    }
    g_status_led_mode = mode;
    if (!g_status_led_state_known || lit != g_status_led_lit) {
        set_status_led(lit);
    }
}

bool brd_io_is_charging(void) {
    /* [V0.10 恢復] 僅讀取 DET 電位，不更動上拉模式或 LED 狀態。 */
    return gpio_get_level((gpio_num_t)CHRG_DET_GPIO) == CHRG_ACTIVE_LEVEL;
}

/* [新增] IR TEST：使用原本兩組 GPIO，避免重複初始化或搶占 RPM / LOAD ISR。 */
bool brd_io_read_ir1(void) {
    return gpio_get_level((gpio_num_t)RPM_IR_GPIO) == IR_TEST_TRIGGER_LEVEL;
}

bool brd_io_read_ir2(void) {
    return gpio_get_level((gpio_num_t)LOAD_IR_GPIO) == IR_TEST_TRIGGER_LEVEL;
}
