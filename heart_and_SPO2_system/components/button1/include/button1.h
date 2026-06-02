#ifndef BUTTON1_H
#define BUTTON1_H

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define BUTTON_GPIO_PIN    GPIO_NUM_5  // Chân GPIO kết nối với nút bấm (Active-Low)

// Định nghĩa các Bit sự kiện (Event Bits) cho hệ thống
#define BIT_STATUS_NOT_WEARING    (1 << 0) // Bit 0: Trạng thái Chưa đeo / Nghỉ ngơi
#define BIT_STATUS_WEARING        (1 << 1) // Bit 1: Trạng thái Đang đeo / Theo dõi y tế

// Biến Event Group toàn cục để các Task khác (như Task nhịp tim) có thể sử dụng
extern EventGroupHandle_t g_device_event_group;

/**
 * @brief Khởi tạo phần cứng GPIO, dịch vụ ngắt và Event Group cho nút bấm
 * @return esp_err_t ESP_OK nếu thành công, ngược lại trả về mã lỗi
 */
esp_err_t button1_hardware_init(void);

#endif // BUTTON1_H