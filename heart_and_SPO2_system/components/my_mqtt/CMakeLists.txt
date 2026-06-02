idf_component_register(
    SRCS "my_mqtt.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt esp_event esp_wifi button1
)