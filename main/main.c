#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "led_strip.h"
#include "output_ramp.h"
#include "puff_detector.h"
#include "sdp8xx.h"

static const char *TAG = "main";

/* Each sample is printed as: D,<ms>,<pressure_pa>,<temp_c>,<state>,<reason>,<output_percent> */
#define SAMPLE_PERIOD_MS  10

/* ESP32-C3 pins - change to match your wiring */
#define I2C_SDA_GPIO  4
#define I2C_SCL_GPIO  9
#define ACTIVATE_GPIO 10   /* high while an inhale is confirmed: future coil driver */
#define LED_GPIO      8    /* on-board WS2812 RGB LED, lit together with ACTIVATE_GPIO */

/* Output fade times */
#define OUTPUT_RISE_MS 300
#define OUTPUT_FALL_MS 500

/* Coil PWM on ACTIVATE_GPIO */
#define COIL_PWM_HZ        1000
#define COIL_PWM_MAX_DUTY  1023  /* 10-bit */

#define LED_MAX_RED 80

/* Consecutive failed sensor reads that force the output off */
#define MAX_READ_FAILURES 3

static led_strip_handle_t s_led;

static void led_show(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led) {
        led_strip_set_pixel(s_led, 0, r, g, b);
        led_strip_refresh(s_led);
    }
}

static void led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    if (led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led) != ESP_OK) {
        s_led = NULL;
        ESP_LOGW(TAG, "LED not available");
        return;
    }
    /* Short green flash at boot shows the LED works before any inhale */
    led_show(0, 40, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    led_show(0, 0, 0);
}

static void coil_pwm_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = COIL_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = {
        .gpio_num = ACTIVATE_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

/* Single place that drives the coil output and its indicator together */
static void set_output(float level)
{
    static uint8_t led_red;
    static uint32_t duty;

    uint32_t new_duty = (uint32_t)(level * COIL_PWM_MAX_DUTY);
    if (new_duty != duty) {
        duty = new_duty;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }

    /* Squared so the fade looks even: LEDs look too bright at low levels when driven linearly */
    uint8_t new_red = (uint8_t)(level * level * LED_MAX_RED);
    if (new_red != led_red) {
        led_red = new_red;
        led_show(led_red, 0, 0);
    }
}

void app_main(void)
{
    /* Set the level first so the pin never drives high during start-up */
    ESP_ERROR_CHECK(gpio_set_level(ACTIVATE_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_direction(ACTIVATE_GPIO, GPIO_MODE_OUTPUT));
    coil_pwm_init();
    led_init();

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  /* use external 4.7k-10k pull-ups for reliability */
    };
    i2c_master_bus_handle_t bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));

    if (i2c_master_probe(bus, SDP8XX_I2C_ADDR_SDP8X0, 100) != ESP_OK) {
        ESP_LOGE(TAG, "SDP810 not found at 0x%02X - check wiring / pull-ups", SDP8XX_I2C_ADDR_SDP8X0);
    }

    sdp8xx_t sdp;
    ESP_ERROR_CHECK(sdp8xx_init(&sdp, bus, SDP8XX_I2C_ADDR_SDP8X0));

    uint32_t pn;
    uint64_t sn;
    if (sdp8xx_read_product_id(&sdp, &pn, &sn) == ESP_OK) {
        ESP_LOGI(TAG, "Product number: 0x%08lX, serial: 0x%016llX", (unsigned long)pn, (unsigned long long)sn);
    } else {
        ESP_LOGW(TAG, "Could not read product ID");
    }

    ESP_ERROR_CHECK(sdp8xx_start_continuous(&sdp));

    puff_config_t puff_cfg = puff_default_config();
    puff_detector_t puff;
    puff_init(&puff, &puff_cfg);

    int read_failures = 0;
    float output_level = 0.0f;
    TickType_t last_wake = xTaskGetTickCount();
    while (1) {
        sdp8xx_reading_t r;
        esp_err_t err = sdp8xx_read_measurement(&sdp, &r);
        if (err == ESP_OK) {
            read_failures = 0;
            int64_t now_ms = esp_timer_get_time() / 1000;
            puff_update(&puff, (uint32_t)now_ms, r.differential_pressure_pa);
            /* Machine-readable line for the PC plotter (tools/sdp_plotter.py) */
            printf("D,%lld,%.4f,%.3f,%d,%d,%d\n", now_ms, r.differential_pressure_pa,
                   r.temperature_c, (int)puff.state, (int)puff.reason, (int)(output_level * 100));
        } else {
            read_failures++;
            ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(err));
        }
        if (read_failures >= MAX_READ_FAILURES) {
            output_level = 0.0f;  /* a fault cuts the output at once, with no fade */
        } else {
            output_level = output_ramp_step(output_level, puff.state == PUFF_ACTIVE,
                                            SAMPLE_PERIOD_MS, OUTPUT_RISE_MS, OUTPUT_FALL_MS);
        }
        set_output(output_level);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}
