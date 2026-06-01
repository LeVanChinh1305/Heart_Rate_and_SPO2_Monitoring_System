idf_component_register(
    SRCS "WIFI.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_wifi nvs_flash
)