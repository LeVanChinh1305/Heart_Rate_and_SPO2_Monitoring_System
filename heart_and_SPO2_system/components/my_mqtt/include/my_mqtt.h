#ifndef MY_MQTT_H
#define MY_MQTT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define MQTT_TOPIC_DATA "chinh/health_data"
#define MQTT_TOPIC_RAW "chinh/health_raw"
#define MQTT_TOPIC_CONTROL "chinh/control"
#define MQTT_URI "mqtt://broker.hivemq.com:1883/"

extern volatile float g_heart_rate_max_threshold;
extern volatile float spo2_min_threshold;
extern volatile float temp_min_threshold1;
extern volatile float temp_max_threshold1;
extern volatile float temp_min_threshold2; 
extern volatile float temp_max_threshold2; 

// hàm khởi động mqtt client (1 lần trong main)
esp_err_t mqtt_start_init(void);

// hàm gửi dữ liệu cảm biến lên server
esp_err_t mqtt_pulish_data(float bpm, float spo2, float temp); 

esp_err_t mqtt_publish_raw_single(int32_t raw_sample);

// hàm nhận dữ liệu điều khiển từ server 
bool mqtt_is_connected(void);

#endif 