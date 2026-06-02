#include "my_mqtt.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include <string.h>
#include "mqtt_client.h"
#include "button1.h"

static const char *TAG = "MQTT_DRIVER";
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_is_connected = false;
volatile float g_heart_rate_max_threshold = 140.0f;
volatile float spo2_min_threshold = 92.0f; 

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data){
    // void *handler_args (Dữ liệu tùy biến của người dùng) Lấy từ tham số cuối cùng của hàm esp_mqtt_register_events()
    // esp_event_base_t base (Họ của sự kiện): một chuỗi định danh để phân biệt "nhóm sự kiện" này thuộc về ai.
        // Hệ thống tự điền: (MQTT_EVENTS, WIFI_EVENT hoặc IP_EVENT)
    // int32_t event_id (Mã số định danh sự kiện)
    // void *event_data (Gói dữ liệu đi kèm sự kiện) chứa tất cả những thứ bạn cần từ Web gửi xuống

    // lấy gói dữ liệu từ server
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client; 

    switch ((esp_mqtt_event_id_t) event_id){
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Đã kết nối thành công tới MQTT Broker!");
            s_is_connected = true;
            
            // Đăng ký nhận lệnh từ giao diện Web (QoS 1)
            esp_mqtt_client_subscribe(client, MQTT_TOPIC_CONTROL, 1);
            ESP_LOGI(TAG, "Đã Subscribe thành công hộp thư lệnh: %s", MQTT_TOPIC_CONTROL);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Mất kết nối MQTT Broker! Hệ thống đang tự động liên kết lại...");
            s_is_connected = false;
            break;
        case MQTT_EVENT_DATA: {
            char topic_buff[64] = {0};
            char data_buff[64] = {0};
            
            int topic_len = MIN(event->topic_len, sizeof(topic_buff) - 1);
            int data_len = MIN(event->data_len, sizeof(data_buff) - 1);
            
            memcpy(topic_buff, event->topic, topic_len);
            memcpy(data_buff, event->data, data_len);

            ESP_LOGI(TAG, "[Web Control] -> Lệnh: %s", data_buff);

            // XỬ LÝ SƠ BỘ CÁC LỆNH ĐIỀU KHIỂN TỪ WEB
            if (strcmp(topic_buff, MQTT_TOPIC_CONTROL) == 0) {
                
                // 1. Lệnh Khởi động lại (REBOOT)
                if (strcmp(data_buff, "REBOOT") == 0) {
                    ESP_LOGW(TAG, "Yêu cầu từ Web: Hệ thống sẽ Khởi động lại sau 1 giây...");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                }
                
                // 2. Lệnh Ngủ sâu (DEEP SLEEP)
                else if (strcmp(data_buff, "DEEP_SLEEP") == 0) {
                    ESP_LOGW(TAG, "Yêu cầu từ Web: Đang dọn dẹp tài nguyên để vào DEEP SLEEP...");
                    esp_mqtt_client_stop(client);
                    esp_wifi_stop();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    // Kích hoạt nút bấm cứng vật lý (Ví dụ GPIO 9) để nhấn giữ là tự thức dậy
                    esp_deep_sleep_enable_gpio_wakeup(1ULL << GPIO_NUM_9, ESP_GPIO_WAKEUP_GPIO_LOW);
                    
                    ESP_LOGI(TAG, "Thiết bị bắt đầu ngủ sâu!");
                    esp_deep_sleep_start();
                }
                
                // 3. Lệnh thay đổi ngưỡng nhịp tim (SET_HR_LIMIT:xxx)
                else if (strncmp(data_buff, "SET_HR_LIMIT:", 13) == 0) {
                    float val = 0.0f;
                    if (sscanf(data_buff, "SET_HR_LIMIT:%f", &val) == 1) {
                        if (val >= 40.0f && val <= 200.0f) {
                            g_heart_rate_max_threshold = val;
                            ESP_LOGI(TAG, "Cập nhật thành công ngưỡng nhịp tim tối đa mới: %.1f bpm", g_heart_rate_max_threshold);
                        } else {
                            ESP_LOGE(TAG, "Ngưỡng không hợp lệ! (Dải chuẩn: 40 - 200)");
                        }
                    }
                }

                else if(strncmp(data_buff, "SET_SPO2_LIMIT:", 15)== 0){
                    float val = 0.0f;
                    if (sscanf(data_buff, "SET_SPO2_LIMIT:%f", &val) == 1) {
                        if (val >= 50.0f && val <= 100.0f) {
                            spo2_min_threshold = val;
                            ESP_LOGI(TAG, "Cập nhật thành công ngưỡng nồng độ spo2 min mới: %.1f ", spo2_min_threshold);
                        } else {
                            ESP_LOGE(TAG, "Ngưỡng không hợp lệ! (Dải chuẩn: 50 - 100)");
                        }
                    }
                }

                // Lệnh Bắt đầu theo dõi từ Web (START)
                else if (strcmp(data_buff, "START") == 0) {
                    if (g_device_event_group != NULL) {
                        // Web ra lệnh đo -> Xóa trạng thái THÁO MÁY, Kích hoạt trạng thái ĐANG ĐEO
                        xEventGroupClearBits(g_device_event_group, BIT_STATUS_NOT_WEARING);
                        xEventGroupSetBits(g_device_event_group, BIT_STATUS_WEARING);
                        ESP_LOGI(TAG, "Lệnh Web: Đã đồng bộ chuyển mạch sang chế độ ĐANG ĐEO");
                    }
                }

                //  Lệnh Dừng theo dõi từ Web (STOP)
                else if (strcmp(data_buff, "STOP") == 0) {
                    if (g_device_event_group != NULL) {
                        // Web ra lệnh dừng -> Xóa trạng thái ĐANG ĐEO, Kích hoạt trạng thái THÁO MÁY
                        xEventGroupClearBits(g_device_event_group, BIT_STATUS_WEARING);
                        xEventGroupSetBits(g_device_event_group, BIT_STATUS_NOT_WEARING);
                        ESP_LOGW(TAG, "Lệnh Web: Đã đồng bộ chuyển mạch sang chế độ THÁO MÁY");
                    }
                }
                
            }
            break;
        }
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Phát hiện lỗi xung đột luồng dữ liệu MQTT");
            break;
        default:
            break;
    }
}

esp_err_t mqtt_start_init(void){
    if(s_client != NULL) return ESP_OK; // trường hợp đã khởi tạo trước đó 
    
    // cấu hình thông số 
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri = MQTT_URI,
    };
    s_client = esp_mqtt_client_init(&mqtt_config);
    if(s_client == NULL){
        ESP_LOGE(TAG, "khởi tạo mqtt thất bại");
        return ESP_FAIL;
    }

    // đăng ký hàm bắt dự kiện event handle 
    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký Event Handler thất bại!");
        return err;
    }

    // Kích hoạt Task ngầm xử lý mạng của MQTT hệ thống
    err = esp_mqtt_client_start(s_client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Driver MQTT ngầm khởi động thành công!");
    }
    return err;

}

esp_err_t mqtt_pulish_data(float bpm, float spo2, float temp){
    if (!s_is_connected || s_client == NULL) {
        return ESP_ERR_INVALID_STATE; // Mạng chưa sẵn sàng để đẩy dữ liệu
    }

    // Kiểm tra trạng thái đeo máy trước khi gửi dữ liệu 
    if (g_device_event_group != NULL) {
        EventBits_t bits = xEventGroupGetBits(g_device_event_group);
        if (!(bits & BIT_STATUS_WEARING)) {
            return ESP_ERR_INVALID_STATE; // Đang tháo máy thì không gửi dữ liệu sinh hiệu
        }
    }

    char payload[128];
    // Đóng gói dữ liệu thành chuỗi định dạng JSON tiêu chuẩn để Web dễ phân tích (parse)
    int len = snprintf(payload, sizeof(payload), 
                       "{\"bpm\":%.1f,\"spo2\":%.1f,\"body_temp\":%.1f}", 
                       bpm, spo2, temp);

    // Tiến hành gửi dữ liệu với QoS 1 để đảm bảo tính an toàn dữ liệu sinh hiệu
    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_DATA, payload, len, 1, 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Lỗi đóng gói hoặc nghẽn mạng! Gửi gói tin thất bại.");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Đã gửi thành công JSON lên Web [ID:%d]", msg_id);
    return ESP_OK;
}

esp_err_t mqtt_publish_raw_single(int32_t raw_sample) {
    if (!s_is_connected || s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_device_event_group != NULL) {
        EventBits_t bits = xEventGroupGetBits(g_device_event_group);
        if (!(bits & BIT_STATUS_WEARING)) {
            return ESP_ERR_INVALID_STATE; // Đang tháo máy thì không gửi dữ liệu sinh hiệu
        }
    }

    // --- THUẬT TOÁN LỌC DC REMOVAL FILTER (Hệ số alpha = 0.95 đến 0.99) ---
    static float s_w = 0.0f;
    float alpha = 0.95f;
    
    float current_w = raw_sample + alpha * s_w;
    int32_t filtered_val = (int32_t)(current_w - s_w);
    s_w = current_w; // Lưu lại cho mẫu kế tiếp
    // ---------------------------------------------------------------------

    char payload[64];
    // Gửi giá trị sau lọc (lúc này dao động cực đẹp quanh trục số 0, ví dụ: -150, -20, 50, 200...)
    int len = snprintf(payload, sizeof(payload), 
                       "{\"deviceId\":\"ESP32C6_01\",\"val\":%ld}", 
                       (long)filtered_val);

    int msg_id = esp_mqtt_client_publish(s_client, MQTT_TOPIC_RAW, payload, len, 0, 0);
    
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool mqtt_is_connected(void){
    return s_is_connected; 
}