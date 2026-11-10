#include "sdp8xx.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"

static const char *TAG = "sdp8xx";

#define I2C_TIMEOUT_MS 100

/* Commands (datasheet / Sensirion arduino-i2c-sdp) */
#define CMD_START_CONT_DP_AVG       0x3615  /* differential pressure, average till read */
#define CMD_STOP_CONT               0x3FF9
#define CMD_TRIGGER_DP_NO_STRETCH   0x362F
#define CMD_READ_PRODUCT_ID_1       0x367C
#define CMD_READ_PRODUCT_ID_2       0xE102

#define TEMPERATURE_SCALE           200.0f

static uint8_t sensirion_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t send_cmd(sdp8xx_t *s, uint16_t cmd)
{
    uint8_t buf[2] = { cmd >> 8, cmd & 0xFF };
    return i2c_master_transmit(s->dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

/* Reads n_words 16-bit words, each followed by a CRC byte */
static esp_err_t read_words(sdp8xx_t *s, uint16_t *words, size_t n_words)
{
    uint8_t buf[18];
    size_t len = n_words * 3;
    if (len > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(i2c_master_receive(s->dev, buf, len, I2C_TIMEOUT_MS), TAG, "i2c read failed");
    for (size_t i = 0; i < n_words; i++) {
        const uint8_t *p = &buf[i * 3];
        if (sensirion_crc8(p, 2) != p[2]) {
            ESP_LOGW(TAG, "CRC mismatch on word %u", (unsigned)i);
            return ESP_ERR_INVALID_CRC;
        }
        words[i] = ((uint16_t)p[0] << 8) | p[1];
    }
    return ESP_OK;
}

static void convert(const uint16_t *w, sdp8xx_reading_t *out)
{
    int16_t dp_raw = (int16_t)w[0];
    int16_t t_raw = (int16_t)w[1];
    int16_t scale = (int16_t)w[2];   /* 60 for SDP810-500Pa, 240 for SDP810-125Pa */
    out->differential_pressure_pa = scale ? (float)dp_raw / (float)scale : 0.0f;
    out->temperature_c = (float)t_raw / TEMPERATURE_SCALE;
}

esp_err_t sdp8xx_init(sdp8xx_t *s, i2c_master_bus_handle_t bus, uint8_t addr)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &cfg, &s->dev), TAG, "add device failed");

    /* Sensor needs up to 25 ms after power-up before it accepts commands */
    vTaskDelay(pdMS_TO_TICKS(30));

    /* Sensor may still be in continuous mode after an MCU reset: stop it (ignore NACK). */
    send_cmd(s, CMD_STOP_CONT);
    vTaskDelay(pdMS_TO_TICKS(2));
    return ESP_OK;
}

esp_err_t sdp8xx_read_product_id(sdp8xx_t *s, uint32_t *product_number, uint64_t *serial_number)
{
    ESP_RETURN_ON_ERROR(send_cmd(s, CMD_READ_PRODUCT_ID_1), TAG, "cmd1");
    ESP_RETURN_ON_ERROR(send_cmd(s, CMD_READ_PRODUCT_ID_2), TAG, "cmd2");
    uint16_t w[6];
    ESP_RETURN_ON_ERROR(read_words(s, w, 6), TAG, "read id");
    *product_number = ((uint32_t)w[0] << 16) | w[1];
    *serial_number = ((uint64_t)w[2] << 48) | ((uint64_t)w[3] << 32) |
                     ((uint64_t)w[4] << 16) | w[5];
    return ESP_OK;
}

esp_err_t sdp8xx_start_continuous(sdp8xx_t *s)
{
    ESP_RETURN_ON_ERROR(send_cmd(s, CMD_START_CONT_DP_AVG), TAG, "start cont");
    vTaskDelay(pdMS_TO_TICKS(20));  /* first accurate value after ~20 ms */
    return ESP_OK;
}

esp_err_t sdp8xx_stop_continuous(sdp8xx_t *s)
{
    ESP_RETURN_ON_ERROR(send_cmd(s, CMD_STOP_CONT), TAG, "stop cont");
    vTaskDelay(pdMS_TO_TICKS(1));
    return ESP_OK;
}

esp_err_t sdp8xx_read_measurement(sdp8xx_t *s, sdp8xx_reading_t *out)
{
    uint16_t w[3];
    ESP_RETURN_ON_ERROR(read_words(s, w, 3), TAG, "read meas");
    convert(w, out);
    return ESP_OK;
}

esp_err_t sdp8xx_triggered_measurement(sdp8xx_t *s, sdp8xx_reading_t *out)
{
    ESP_RETURN_ON_ERROR(send_cmd(s, CMD_TRIGGER_DP_NO_STRETCH), TAG, "trigger");
    vTaskDelay(pdMS_TO_TICKS(50));  /* max conversion time 45 ms */
    return sdp8xx_read_measurement(s, out);
}
