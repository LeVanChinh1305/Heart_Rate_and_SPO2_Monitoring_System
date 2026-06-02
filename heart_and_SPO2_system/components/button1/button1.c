#include <stdio.h>
#include "button1.h"
#include "esp_log.h"
#include "esp_attr.h"

static const char *TAG = "BUTTON1_DRIVER";

// Khởi tạo Handle cho Event Group bằng NULL
EventGroupHandle_t g_device_event_group = NULL;

// Biến lưu mốc thời gian (mili-giây) của cú bấm hợp lệ cuối cùng
static uint32_t last_button_press_time = 0;

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
        } else {
            // Nếu đang Không Đeo -> Chuyển sang ĐANG ĐEO
            xEventGroupClearBitsFromISR(g_device_event_group, BIT_STATUS_NOT_WEARING);
            xEventGroupSetBitsFromISR(g_device_event_group, BIT_STATUS_WEARING, &xHigherPriorityTaskWoken);
        }
        
        // Cập nhật lại mốc thời gian của cái bấm đầu tiên hợp lệ này
        last_button_press_time = now;
        
        // Yêu cầu chuyển ngữ cảnh nếu có Task ưu tiên cao hơn bị đánh thức
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
    // Tất cả các ngắt sinh ra do dội phím hoặc tì đè trong vòng 300ms sẽ bị bỏ qua hoàn toàn
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

    // 2. Cấu hình Driver GPIO cho nút bấm
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

    // 3. Đăng ký hàm xử lý ngắt cụ thể cho chân nút bấm này
    err = gpio_isr_handler_add(BUTTON_GPIO_PIN, button_gpio_isr_handler, (void*) BUTTON_GPIO_PIN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Đăng ký hàm ngắt ISR thất bại: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Khởi tạo thành công Driver nút bấm (Chân %d). Chế độ: Chỉ lấy tín hiệu đầu tiên.", BUTTON_GPIO_PIN);
    return ESP_OK;
}