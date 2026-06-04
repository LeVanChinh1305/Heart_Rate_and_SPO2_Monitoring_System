#include <stdio.h>
#include "button1.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "my_mqtt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BUTTON1_DRIVER";

// Khởi tạo Handle cho Event Group bằng NULL
EventGroupHandle_t g_device_event_group = NULL;

// Biến lưu mốc thời gian (mili-giây) của cú bấm hợp lệ cuối cùng
static uint32_t last_button_press_time = 0;
extern bool status_measure;
extern bool status_active;

// Thêm Handle quản lý Task xử lý tác vụ nút bấm sau ngắt
static TaskHandle_t button_task_handle = NULL;

/**
 * @brief Task xử lý các tác vụ nặng (như gửi MQTT) ngoài luồng ISR
 */
static void button_processing_task(void *pvParameters) {
    while (1) {
        // Task rơi vào trạng thái Block an toàn, đợi lệnh kích hoạt từ hàm ngắt
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        ESP_LOGI(TAG, "Đã nhận tín hiệu từ nút bấm, tiến hành gửi MQTT...");
        
        // Gọi các hàm mạng ở đây là AN TOÀN TUYỆT ĐỐI vì đang ở môi trường Task thường
        if (mqtt_is_connected()) {
            mqtt_publish_status(status_active, status_measure);
        }
    }
}

/**
 * @brief Hàm ngắt vật lý (ISR) xử lý sự kiện bấm nút
 * Sử dụng cơ chế khóa thời gian (Debounce) để chặn tì đè và nhiễu nhả phím
 */
static void IRAM_ATTR button_gpio_isr_handler(void* arg) {
    // Lấy thời gian hệ thống hiện tại ở tầng ISR (tính bằng mili-giây)
    uint32_t now = xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    
    // CHẶN LỆNH TÌ ĐÈ: Chỉ xử lý nếu khoảng cách giữa 2 lần kích hoạt > 300ms
    if (now - last_button_press_time > 300) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        
        // Đọc nhanh các bit hiện tại trong Event Group từ ISR
        EventBits_t current_bits = xEventGroupGetBitsFromISR(g_device_event_group);
        
        if (current_bits & BIT_STATUS_WEARING) {
            // Nếu đang Đeo -> Chuyển sang KHÔNG ĐEO
            xEventGroupClearBitsFromISR(g_device_event_group, BIT_STATUS_WEARING);
            xEventGroupSetBitsFromISR(g_device_event_group, BIT_STATUS_NOT_WEARING, &xHigherPriorityTaskWoken);
            status_measure = false;
        } else {
            // Nếu đang Không Đeo -> Chuyển sang ĐANG ĐEO
            xEventGroupClearBitsFromISR(g_device_event_group, BIT_STATUS_NOT_WEARING);
            xEventGroupSetBitsFromISR(g_device_event_group, BIT_STATUS_WEARING, &xHigherPriorityTaskWoken);
            status_measure = true;
        }
        
        // GIẢI PHÓNG HÀM NGẮT: Đánh thức Task xử lý mạng dậy để gửi MQTT
        if (button_task_handle != NULL) {
            vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);
        }
        
        // Cập nhật lại mốc thời gian của cái bấm đầu tiên hợp lệ này
        last_button_press_time = now;
        
        // Yêu cầu chuyển ngữ cảnh nếu có Task ưu tiên cao hơn bị đánh thức
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

esp_err_t button1_hardware_init(void) {
    // 1. Khởi tạo Event Group
    g_device_event_group = xEventGroupCreate();
    if (g_device_event_group == NULL) {
        ESP_LOGE(TAG, "Không thể tạo FreeRTOS Event Group!");
        return ESP_FAIL;
    }
    
    // Đặt trạng thái mặc định ban đầu là CHƯA ĐEO khi vừa bật nguồn
    xEventGroupSetBits(g_device_event_group, BIT_STATUS_NOT_WEARING);

    // 2. Tạo Task ngầm xử lý gửi MQTT trước khi đăng ký ngắt phím
    xTaskCreate(button_processing_task, "button_task", 4096, NULL, 5, &button_task_handle);

    // 3. Cấu hình Driver GPIO cho nút bấm
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,            // Kích hoạt ngắt tại CẠNH XUỐNG (khi nhấn phím)
        .pin_bit_mask = (1ULL << BUTTON_GPIO_PIN), // Chọn chân GPIO cấu hình
        .mode = GPIO_MODE_INPUT,                  // Thiết lập hướng Đầu vào (Input)
        .pull_up_en = GPIO_PULLUP_ENABLE,         // Bật điện trở kéo lên nội tại (treo chân ở 3.3V)
        .pull_down_en = GPIO_PULLDOWN_DISABLE     // Tắt điện trở kéo xuống
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cấu hình GPIO thất bại: %s", esp_err_to_name(err));
        return err;
    }

    // 4. Đăng ký hàm xử lý ngắt cụ thể cho chân nút bấm này
    err = gpio_isr_handler_add(BUTTON_GPIO_PIN, button_gpio_isr_handler, (void*) BUTTON_GPIO_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký hàm ngắt ISR thất bại: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Khởi tạo thành công Driver nút bấm (Chân %d). Chế độ: Tách luồng ISR an toàn.", BUTTON_GPIO_PIN);
    return ESP_OK;
}