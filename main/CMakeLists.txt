idf_component_register(SRCS "main.c" "sdp8xx.c" "puff_detector.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_timer)
