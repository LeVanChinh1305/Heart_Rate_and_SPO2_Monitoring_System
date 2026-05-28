#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"

// Include drivers và thuật toán xử lý
#include "MAX30102.h"
#include "MLX90614.h"
#include "ppg_algorithm.h"

static const char *TAG = "MAIN_APP";

// Cấu hình chân cứng cho ESP32-C6 / ESP32 
#define I2C_MASTER_SDA_IO           GPIO_NUM_6    
#define I2C_MASTER_SCL_IO           GPIO_NUM_7   
#define MAX30102_INT_GPIO           GPIO_NUM_4   // Chân kết nối tới chân INT của cảm biến MAX30102

// -----------------------------------------------------------------------------
// [FIX LỖI BIÊN DỊCH]: Khai báo cấu trúc gom nhóm các handle thiết bị I2C
// -----------------------------------------------------------------------------
typedef struct {
    max30102_dev_t *max30102_dev;
    i2c_master_dev_handle_t mlx90614_dev;
} temp_task_args_t;

// Handle lưu trữ Task điều phối dữ liệu từ hàng đợi FIFO
static TaskHandle_t max30102_task_handle = NULL;

// Task ngắt ISR cho chân INT của max30102
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

// Task tính toán HR và SpO2 từ dữ liệu thô đọc được, chạy dựa trên thông báo ngắt ISR
void max30102_processing_task(void *pvParameters)
{
    max30102_dev_t *max30102_dev = (max30102_dev_t *)pvParameters;
    max30102_sample_t sample_buffer[32]; // Mảng chứa mẫu đọc burst read
    uint8_t samples_read = 0;
    uint8_t status1 = 0, status2 = 0;

    // Biến cấu trúc cục bộ dùng để nhận kết quả đầu ra của thuật toán xử lý tín hiệu PPG
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
                    //    printf("RED:%ld,IR:%ld\n", sample_buffer[i].red, sample_buffer[i].ir);
                        
                        // Gửi mẫu vào thuật toán xử lý tín hiệu PPG để tính toán HR và SpO2
                        if(ppg_algorithm_process_sample(sample_buffer[i].red, sample_buffer[i].ir, &ppg_result)) {
                            // Nếu có kết quả mới sau mỗi 400 mẫu, in ra màn hình log 
                            if(ppg_result.valid){
                                ESP_LOGI(TAG, "=> KẾT QUẢ ĐO: HR: %.1f bpm, Spo2: %.1f%%", ppg_result.heart_rate, ppg_result.spo2);
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

// Task đọc nhiệt độ tổng hợp định kỳ từ cả MAX30102 (Nội bộ) và MLX90614 (Hồng ngoại không tiếp xúc)
void mlx90614_temperature_task(void *pvParameters)
{
    temp_task_args_t *args = (temp_task_args_t *)pvParameters;
    float max30102_temp = 0.0f;
    float body_temp_ambient = 0.0f;
    float body_temp_object = 0.0f;

    ESP_LOGI(TAG, "Task giám sát nhiệt độ tổng hợp bắt đầu chạy.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi định kỳ mỗi 5 giây

        // 1. Đọc nhiệt độ bo mạch/chip của cảm biến nhịp tim MAX30102
        if (max30102_read_temperature(args->max30102_dev, &max30102_temp) == ESP_OK) {
            ESP_LOGI("TEMP_MONITOR", "Nhiệt độ bo mạch MAX30102: %.2f °C", max30102_temp);
            if (max30102_temp > 45.0f) {
                ESP_LOGW("TEMP_MONITOR", "Cảnh báo: Chip MAX30102 đang quá nóng!");
            }
        } else {
            ESP_LOGE("TEMP_MONITOR", "Không thể đọc nhiệt độ chip nội bộ MAX30102!");
        }

        // 2. Đọc nhiệt độ từ cảm biến hồng ngoại MLX90614
        esp_err_t err_ta = mlx90614_read_ambient(args->mlx90614_dev, &body_temp_ambient);
        esp_err_t err_to = mlx90614_read_object(args->mlx90614_dev, &body_temp_object);

        if (err_ta == ESP_OK && err_to == ESP_OK) {
            // Khi làm đồng hồ đeo tay, bạn có thể thực hiện một hằng số cộng bù sai số nhiệt độ da
            ESP_LOGI("TEMP_MONITOR", "MLX90614 -> Môi trường: %.2f °C | Bề mặt da cổ tay: %.2f °C", 
                     body_temp_ambient, body_temp_object);
        } else {
            ESP_LOGE("TEMP_MONITOR", "Lỗi hoặc mất kết nối đường truyền I2C với MLX90614!");
        }
    }
}

// 4. HÀM MAIN CHÍNH
void app_main(void)
{
    ESP_LOGI(TAG, "Đang khởi tạo hệ thống...");
    
    // Khởi tạo trạng thái bộ đệm thuật toán xử lý tín hiệu PPG trước khi lấy mẫu
    ppg_algorithm_init(); 

    // BƯỚC 1: Cấu hình Bus I2C Master tổng (Đường truyền chung cho toàn mạch)
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // Bật pull-up nội bộ nếu mạch ngoài không hàn trở treo ngoài
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    // BƯỚC 2: Định nghĩa thông số cấu hình hoạt động cho MAX30102
    max30102_config_t sensor_config = {
        .mode = MAX30102_MODE_SPO2,                         // Đo song song cả Red và IR 
        .adc_range = MAX30102_SPO2_ADC_RGE_4096nA,          // Dải đo dòng quang dòng ADC 
        .sample_rate = MAX30102_SPO2_SR_100,                // Tốc độ 100 mẫu / giây 
        .pulse_width = MAX30102_SPO2_PW_411us_18b,          // Độ rộng xung max độ phân giải 18-bit 
        .led_current_red = MAX30102_LED_CURRENT_7MA,        // Dòng phát LED Red ~7.2mA 
        .led_current_ir = MAX30102_LED_CURRENT_7MA,         // Dòng phát LED IR ~7.2mA 
        .fifo_almost_full = MAX30102_FIFO_ALMOST_FULL_VAL_8, // Trống còn 8 vị trí là đá ngắt 
        .enable_fifo_rollover = true                        // Kích hoạt ghi vòng đè khi đầy bộ nhớ đệm
    };

    // BƯỚC 3: Đăng ký khởi tạo Driver thiết bị MAX30102
    static max30102_dev_t max30102_device;
    esp_err_t ret;
    int retry_count = 0;
    const int max_retries = 100; // Thử lại tối đa 100 lần vòng lặp kiểm tra dây

    while (retry_count < max_retries) {
        ret = max30102_init(&max30102_device, bus_handle, &sensor_config);
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Khởi tạo cảm biến MAX30102 thành công sau %d lần thử!", retry_count + 1);
            break;
        }

        retry_count ++;
        ESP_LOGW(TAG, "Khởi tạo cảm biến max30102 thất bại lần %d. Thử lại sau 1 giây...", retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Khởi tạo cảm biến max30102 thất bại dữ dội! Dừng hệ thống.");
        return; 
    }

    // BƯỚC 3.2: Đăng ký cấu hình và bắt tay kiểm tra thiết bị MLX90614
    i2c_master_dev_handle_t mlx90614_device_handle = NULL;
    if (mlx90614_init(bus_handle, &mlx90614_device_handle) == ESP_OK) {
        if (mlx90614_check_connection(mlx90614_device_handle) == ESP_OK) {
            ESP_LOGI(TAG, "Khởi tạo và cấu hình kết nối thành công MLX90614!");
        } else {
            ESP_LOGE(TAG, "Mạch phần cứng MLX90614 không phản hồi trên Bus I2C!");
            return;
        }
    } else {
        ESP_LOGE(TAG, "Không thể đăng ký Driver MLX90614 vào cấu trúc Master Bus!");
        return;
    }

    // BƯỚC 4: Cấu hình Chân điện áp ngắt GPIO cho chân INT của cảm biến tim mạch
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,            // Kích hoạt ngắt cạnh xuống (Falling Edge)
        .mode = GPIO_MODE_INPUT,                   // Cấu hình chân chức năng Input
        .pin_bit_mask = (1ULL << MAX30102_INT_GPIO),// Chỉ định ánh xạ bitmask cho pin INT
        .pull_down_en = 0,                         // Vô hiệu hóa pull down
        .pull_up_en = 1,                           // Bật điện trở kéo lên pull-up bắt buộc cho chân Open-Drain
    };
    gpio_config(&io_conf);

    // Kích hoạt dịch vụ ISR quản lý các vector ngắt ngoại vi GPIO toàn cục
    gpio_install_isr_service(0);
    // Gán hàm xử lý ngắt cụ thể với chân GPIO vật lý
    gpio_isr_handler_add(MAX30102_INT_GPIO, max30102_gpio_isr_handler, NULL);

    // Đóng gói an toàn các tham số handle vào vùng nhớ static để nạp làm đối số cho Task FreeRTOS
    static temp_task_args_t temp_args;
    temp_args.max30102_dev = &max30102_device;
    temp_args.mlx90614_dev = mlx90614_device_handle;
    
    // BƯỚC 5: Khởi chạy các FreeRTOS Tasks luồng thực thi nền độc lập
    // Luồng đo đạc tính toán xử lý sóng PPG được ưu tiên tối cao (Priority 10) để tránh bị trễ mất mẫu
    xTaskCreate(max30102_processing_task, "max30102_task", 4096, &max30102_device, 10, &max30102_task_handle);    
    
    // [ĐÃ SỬA]: Luồng giám sát nhiệt độ tổng hợp hai cảm biến chạy song song chu kỳ chậm (Priority 2)
    xTaskCreate(mlx90614_temperature_task, "mlx_temp_task", 4096, &temp_args, 2, NULL);
}