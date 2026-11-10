#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SDP8x0 (e.g. SDP810-125Pa, SDP810-500Pa) = 0x25, SDP8x1 = 0x26 */
#define SDP8XX_I2C_ADDR_SDP8X0  0x25
#define SDP8XX_I2C_ADDR_SDP8X1  0x26

typedef struct {
    i2c_master_dev_handle_t dev;
} sdp8xx_t;

typedef struct {
    float differential_pressure_pa;
    float temperature_c;
} sdp8xx_reading_t;

esp_err_t sdp8xx_init(sdp8xx_t *sensor, i2c_master_bus_handle_t bus, uint8_t addr);
esp_err_t sdp8xx_read_product_id(sdp8xx_t *sensor, uint32_t *product_number, uint64_t *serial_number);
/* Continuous mode, differential pressure, averaging until read */
esp_err_t sdp8xx_start_continuous(sdp8xx_t *sensor);
esp_err_t sdp8xx_stop_continuous(sdp8xx_t *sensor);
/* Reads the latest value in continuous mode */
esp_err_t sdp8xx_read_measurement(sdp8xx_t *sensor, sdp8xx_reading_t *out);
/* Single-shot measurement (sensor must NOT be in continuous mode) */
esp_err_t sdp8xx_triggered_measurement(sdp8xx_t *sensor, sdp8xx_reading_t *out);

#ifdef __cplusplus
}
#endif
