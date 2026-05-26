
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"

// Include driver của bạn
#include "MAX30102.h"
#include "ppg_algorithm.h"

static const char *TAG = "MAIN_APP";

// Cấu hình chân cứng cho ESP32-C6 / ESP32 (Thay đổi tùy thuộc vào bo mạch của bạn)
#define I2C_MASTER_SDA_IO           GPIO_NUM_6    
#define I2C_MASTER_SCL_IO           GPIO_NUM_7   
#define MAX30102_INT_GPIO           GPIO_NUM_4   // Chân kết nối tới chân INT của cảm biến

// Handle lưu trữ Task điều phối dữ liệu
static TaskHandle_t max30102_task_handle = NULL;

// ============================================
// 1. TRÌNH PHỤC VỤ NGẮT (ISR)
// ============================================
static void IRAM_ATTR max30102_gpio_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (max30102_task_handle != NULL) {
        // Gửi thông báo trực tiếp để unlock Task đọc dữ liệu từ FIFO
        vTaskNotifyGiveFromISR(max30102_task_handle, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ============================================
// 2. TASK XỬ LÝ DỮ LIỆU SENSOR
// ============================================
void max30102_processing_task(void *pvParameters)
{
    max30102_dev_t *max30102_dev = (max30102_dev_t *)pvParameters;
    max30102_sample_t sample_buffer[32]; // Mảng chứa mẫu đọc burst read
    uint8_t samples_read = 0;
    uint8_t status1 = 0, status2 = 0;

    // biến cấu trúc cụ bộ dùng để nhận kết quả đầu ra của thuật toán xử lý tín hiệu PPG
    ppg_result_t ppg_result;


    ESP_LOGI(TAG, "Task xử lý MAX30102 bắt đầu hoạt động...");

    while (1) {
        // Chờ tín hiệu ngắt từ chân GPIO (Timeout vô hạn)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Đọc thanh ghi ngắt để biết nguyên nhân và đồng thời xóa cờ ngắt phần cứng
        if (max30102_get_interrupt_status(max30102_dev, &status1, &status2) == ESP_OK) {
            
            // Kiểm tra ngắt FIFO Almost Full hoặc New Data Ready
            if (status1 & (MAX30102_INT_A_FULL | MAX30102_INT_PPG_RDY)) {
                
                // Đọc toàn bộ các mẫu đang có sẵn trong FIFO bằng kĩ thuật Burst Read
                esp_err_t err = max30102_read_samples(max30102_dev, sample_buffer, 32, &samples_read);
                if (err == ESP_OK && samples_read > 0) {
                    for (int i = 0; i < samples_read; i++) {
                        // In ra màn hình dữ liệu thô dạng Serial Plotter để vẽ đồ thị
                        printf("RED:%ld,IR:%ld\n", sample_buffer[i].red, sample_buffer[i].ir);
                        // Gửi mẫu vào thuật toán xử lý tín hiệu PPG để tính toán HR và SpO2
                        // hàm trả về true nếu tích đủ 400 mẫu để tính toán, chu kỳ tính toán sẽ là 4 giây nếu sample_rate là 100 SPs, chu kỳ gối đầu là 2 giây 
                        if(ppg_algorithm_process_sample(sample_buffer[i].red, sample_buffer[i].ir, &ppg_result)) {
                            // Nếu có kết quả mới sau mỗi 400 mẫu, in ra màn hình log 
                            if(ppg_result.valid){
                                ESP_LOGI(TAG, "HR: %f bpm, Spo2: %.1f%%", ppg_result.heart_rate, ppg_result.spo2);
                                // gửi kết quả này 
                            }else{
                                ESP_LOGW(TAG, "Kết quả không hợp lệ do tín hiệu yếu hoặc nhiễu quá nhiều!");
                            }
                        }
                    }
                }
            }

            // Kiểm tra ngắt khi bộ Cancellation ánh sáng môi trường bị tràn bão hòa
            if (status1 & MAX30102_INT_ALC_OVF) {
                ESP_LOGW(TAG, "Ánh sáng môi trường quá mạnh! Hãy che bớt cảm biến.");
            }
        }
    }
}

// ============================================
// 3. TASK PHỤ ĐỌC NHIỆT ĐỘ (Mỗi 5 giây)
// ============================================
void temperature_task(void *pvParameters)
{
    max30102_dev_t *max30102_dev = (max30102_dev_t *)pvParameters;
    float current_temp = 0.0f;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi 5 giây
        if (max30102_read_temperature(max30102_dev, &current_temp) == ESP_OK) {
            ESP_LOGI(TAG, "Nhiệt độ chip hiện tại: %.2f °C", current_temp);
        } else {
            ESP_LOGE(TAG, "Không thể đọc nhiệt độ!");
        }
    }
}

// ============================================
// 4. HÀM MAIN CHÍNH
// ============================================
void app_main(void)
{
    
    ESP_LOGI(TAG, "Đang khởi tạo hệ thống...");
    ppg_algorithm_init(); // Khởi tạo thuật toán xử lý tín hiệu PPG trước khi bắt đầu lấy mẫu từ cảm biến
    // --------------------------------------------
    // BƯỚC 1: Cấu hình Bus I2C Master (Driver mới)
    // --------------------------------------------
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // Bật pull-up nội bộ nếu mạch ngoài không có
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    // --------------------------------------------
    // BƯỚC 2: Định nghĩa cấu hình động cho MAX30102
    // --------------------------------------------
    max30102_config_t sensor_config = {
        .mode = MAX30102_MODE_SPO2,                         // Đo cả Red và IR 
        .adc_range = MAX30102_SPO2_ADC_RGE_4096nA,         // Dải đo dòng ADC 
        .sample_rate = MAX30102_SPO2_SR_100,                // 100 mẫu / giây 
        .pulse_width = MAX30102_SPO2_PW_411us_18b,          // Độ rộng xung max 18-bit 
        .led_current_red = MAX30102_LED_CURRENT_7MA,        // Dòng định thiên LED Red ~7.2mA 
        .led_current_ir = MAX30102_LED_CURRENT_7MA,         // Dòng định thiên LED IR ~7.2mA 
        .fifo_almost_full = MAX30102_FIFO_ALMOST_FULL_VAL_8, // Trống 8 vị trí là báo ngắt 
        .enable_fifo_rollover = true                        // Cho phép ghi vòng vòng nếu quá tải 
    };

    // --------------------------------------------
    // BƯỚC 3: Khởi tạo Driver thiết bị
    // --------------------------------------------
    static max30102_dev_t max30102_device;
    esp_err_t ret;
    int retry_count = 0;
    const int max_retries = 100; // Thử lại tối đa 10 lần

    while (retry_count < max_retries) {
        ret = max30102_init(&max30102_device, bus_handle, &sensor_config);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Khởi tạo cảm biến thành công sau %d lần thử!", retry_count + 1);
            break;
        }

        retry_count ++;
        ESP_LOGW(TAG, "Khởi tạo thất bại lần %d. Thử lại sau 1 giây... (Hãy kiểm tra/ấn chặt lại dây nối)", retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000)); // Đợi 1 giây rồi thử lại
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo cảm biến thất bại dữ dội sau %d lần thử! Dừng hệ thống.", max_retries);
        return; // Thất bại hoàn toàn mới thoát hẳn
    }

    // --------------------------------------------
    // BƯỚC 4: Cấu hình Chân ngắt GPIO cho INT
    // --------------------------------------------
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,            // Kích hoạt khi chân kéo xuống mức THẤP (Falling Edge)
        .mode = GPIO_MODE_INPUT,                   // Cấu hình là chân Input
        .pin_bit_mask = (1ULL << MAX30102_INT_GPIO),// Chỉ định pin INT
        .pull_down_en = 0,                         // Không pull down
        .pull_up_en = 1,                           // Bật pull-up vì chân INT của MAX30102 là Open-Drain
    };
    gpio_config(&io_conf);

    // Cài đặt dịch vụ quản lý ngắt GPIO
    gpio_install_isr_service(0);
    // Liên kết chân GPIO cụ thể với hàm xử lý ngắt ISR
    gpio_isr_handler_add(MAX30102_INT_GPIO, max30102_gpio_isr_handler, NULL);

    // --------------------------------------------
    // BƯỚC 5: Khởi tạo các FreeRTOS Tasks
    // --------------------------------------------
    // Task xử lý dữ liệu được ưu tiên cao hơn (Priority 10) nhằm tránh mất mát mẫu tin trong hàng đợi
    xTaskCreate(max30102_processing_task, "max30102_task", 4096, &max30102_device, 10, &max30102_task_handle);    
    // Task đo nhiệt độ chạy nền tĩnh lặng (Priority 2)
    xTaskCreate(temperature_task, "temp_task", 4096, &max30102_device, 2, NULL);
}
