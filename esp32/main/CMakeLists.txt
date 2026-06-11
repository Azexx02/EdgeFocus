idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_driver_i2c esp_driver_gpio ssd1306 bt)
