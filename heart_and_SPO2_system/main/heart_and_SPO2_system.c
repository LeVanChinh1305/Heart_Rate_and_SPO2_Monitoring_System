#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h" //  Thư viện quản lý Mutex/Semaphore
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h" //  Thư viện SPI Master gốc của ESP-IDF
#include "esp_log.h"
#include "esp_err.h"

// Include drivers và thuật toán xử lý
#include "MAX30102.h"
#include "MLX90614.h"
#include "ppg_algorithm.h"
#include "st7735.h"           //  Khai báo driver LCD tự viết của bạn

static const char *TAG = "MAIN_APP";

// Cấu hình chân I2C cho Cảm biến (Giữ nguyên)
#define I2C_MASTER_SDA_IO           GPIO_NUM_6    
#define I2C_MASTER_SCL_IO           GPIO_NUM_7   
#define MAX30102_INT_GPIO           GPIO_NUM_4   

//  Định nghĩa cụm chân kết nối thực tế của màn hình ST7735
#define LCD_HOST                    SPI2_HOST
#define PIN_NUM_SCK                 GPIO_NUM_0
#define PIN_NUM_SDA                 GPIO_NUM_1  // Đường MOSI dữ liệu xuống
#define PIN_NUM_RST                 GPIO_NUM_2
#define PIN_NUM_AO                  GPIO_NUM_3  // Chân Data/Command Select
#define PIN_NUM_CS                  GPIO_NUM_8
#define PIN_NUM_LED                 GPIO_NUM_9  // Chân điều khiển đèn nền

//  Cấu trúc lưu trữ dữ liệu sức khỏe tổng hợp toàn hệ thống
typedef struct {
    float heart_rate;
    float spo2;
    float body_temp;
    bool ppg_valid;
} health_data_t;

//  Gom nhóm đối số nạp vào luồng giám sát nhiệt độ định kỳ
typedef struct {
    max30102_dev_t *max30102_dev;
    i2c_master_dev_handle_t mlx90614_dev;
} temp_task_args_t;

// Biến toàn cục chia sẻ dữ liệu và Mutex bảo vệ vùng nhớ
static health_data_t g_health_data = {0};
static SemaphoreHandle_t g_data_mutex = NULL;

// Handle quản lý luồng xử lý ngắt của MAX30102
static TaskHandle_t max30102_task_handle = NULL;

// Trình phục vụ ngắt ISR cho chân INT của MAX30102 (Giữ nguyên)
static void IRAM_ATTR max30102_gpio_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (max30102_task_handle != NULL) {
        vTaskNotifyGiveFromISR(max30102_task_handle, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}


void max30102_processing_task(void *pvParameters)
{
    max30102_dev_t *max30102_dev = (max30102_dev_t *)pvParameters;
    max30102_sample_t sample_buffer[32]; 
    uint8_t samples_read = 0;
    uint8_t status1 = 0, status2 = 0;
    ppg_result_t ppg_result;

    ESP_LOGI(TAG, "Task xử lý MAX30102 bắt đầu hoạt động...");

    while (1) {
        // Đã sửa: Chờ ngắt tối đa 1 giây, nếu quá thời gian tự thức dậy xóa cờ kẹt ngắt
        uint32_t is_notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));

        if (is_notified == 0) {
            // Trường hợp mất ngắt (Do nhấc ngón tay): Chủ động đọc thanh ghi để tự xóa cờ phần cứng
            uint8_t s1 = 0, s2 = 0;
            max30102_get_interrupt_status(max30102_dev, &s1, &s2);
            continue; 
        }

        // Trường hợp hệ thống nhận ngắt bình thường
        if (max30102_get_interrupt_status(max30102_dev, &status1, &status2) == ESP_OK) {
            if (status1 & (MAX30102_INT_A_FULL | MAX30102_INT_PPG_RDY)) {
                esp_err_t err = max30102_read_samples(max30102_dev, sample_buffer, 32, &samples_read);
                
                if (err == ESP_OK && samples_read > 0) {
                    for (int i = 0; i < samples_read; i++) {
                        printf("RED:%ld,IR:%ld\n", sample_buffer[i].red, sample_buffer[i].ir);
                        
                        if(ppg_algorithm_process_sample(sample_buffer[i].red, sample_buffer[i].ir, &ppg_result)) {
                            
                            // Chiếm dụng tài nguyên Mutex để đẩy dữ liệu mới vào vùng nhớ dùng chung
                            if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                                g_health_data.heart_rate = ppg_result.heart_rate;
                                g_health_data.spo2 = ppg_result.spo2;
                                g_health_data.ppg_valid = ppg_result.valid;
                                xSemaphoreGive(g_data_mutex); 
                            }

                            if(ppg_result.valid){
                                ESP_LOGI(TAG, "=> KẾT QUẢ ĐO: HR: %.1f bpm, Spo2: %.1f%%", ppg_result.heart_rate, ppg_result.spo2);
                            } else {
                                ESP_LOGW(TAG, "Kết quả không hợp lệ do tín hiệu yếu hoặc nhiễu quá nhiều!");
                            }
                        }
                    }
                }
            }
            if (status1 & MAX30102_INT_ALC_OVF) {
                ESP_LOGW(TAG, "Ánh sáng môi trường quá mạnh! Hãy che bớt cảm biến.");
            }
        }
    }
}


void mlx90614_temperature_task(void *pvParameters)
{
    temp_task_args_t *args = (temp_task_args_t *)pvParameters;
    float max30102_temp = 0.0f;
    float body_temp_ambient = 0.0f;
    float body_temp_object = 0.0f;

    ESP_LOGI(TAG, "Task giám sát nhiệt độ tổng hợp bắt đầu chạy.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Đợi định kỳ 5 giây

        if (max30102_read_temperature(args->max30102_dev, &max30102_temp) == ESP_OK) {
            ESP_LOGI("TEMP_MONITOR", "Nhiệt độ bo mạch MAX30102: %.2f °C", max30102_temp);
        }

        esp_err_t err_ta = mlx90614_read_ambient(args->mlx90614_dev, &body_temp_ambient);
        esp_err_t err_to = mlx90614_read_object(args->mlx90614_dev, &body_temp_object);

        if (err_ta == ESP_OK && err_to == ESP_OK) {
            ESP_LOGI("TEMP_MONITOR", "MLX90614 -> Môi trường: %.2f °C | Cơ thể: %.2f °C", body_temp_ambient, body_temp_object);
            
            if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_health_data.body_temp = body_temp_object;
                xSemaphoreGive(g_data_mutex);
            }
        } else {
            ESP_LOGE("TEMP_MONITOR", "Lỗi đường truyền bus I2C với MLX90614!");
        }
    }
}


void lcd_display_task(void *pvParameters)
{
    st7735_dev_t *lcd_dev = (st7735_dev_t *)pvParameters;
    health_data_t local_data;
    char str_buff[32];

    // Tạo các biến bộ nhớ đệm giao diện để lưu kết quả hợp lệ cuối cùng
    float last_valid_hr = 0.0f;
    float last_valid_spo2 = 0.0f;
    float last_valid_temp = 0.0f;
    bool has_first_data = false; 

    // Khởi tạo màn hình nền đen
    st7735_clear_screen(lcd_dev, 0x0000); 
    
    // Vẽ khung tĩnh duy nhất 1 lần chống nhấp nháy màn hình
    st7735_draw_string(lcd_dev, 8, 10, "SMART WRISTBAND", 0x07FF, 0x0000); // Màu Cyan
    st7735_draw_string(lcd_dev, 0, 28, "----------------", 0xFFFF, 0x0000); // Màu Trắng

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500)); // Quét giao diện định kỳ mỗi 500ms (Không dùng ngắt)

        // Đọc dữ liệu an toàn từ biến toàn cục thông qua Mutex
        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            local_data = g_health_data;
            xSemaphoreGive(g_data_mutex);
        }

        // CHỐT LOGIC: Chỉ giữ lại dữ liệu nếu kết quả tính toán hoàn chỉnh và hợp lệ
        if (local_data.ppg_valid) {
            last_valid_hr = local_data.heart_rate;
            last_valid_spo2 = local_data.spo2;
            has_first_data = true; 
        }

        if (local_data.body_temp > 20.0f) {
            last_valid_temp = local_data.body_temp;
        }

        // --- hiển thị ra màn hình  ---
        if (has_first_data) {
            // Hiển thị Nhịp tim (Cập nhật kết quả hợp lệ cuối cùng)
            snprintf(str_buff, sizeof(str_buff), "Pulse: %.1f BPM ", last_valid_hr);
            st7735_draw_string(lcd_dev, 4, 50, str_buff, 0x07E0, 0x0000); // Màu Xanh Lá
            
            // Hiển thị SpO2
            snprintf(str_buff, sizeof(str_buff), "SpO2 : %.1f %%  ", last_valid_spo2);
            st7735_draw_string(lcd_dev, 4, 75, str_buff, 0x07E0, 0x0000); 
        } else {
            // Trạng thái chờ quét ngón tay lúc vừa bật nguồn
            st7735_draw_string(lcd_dev, 4, 50, "Pulse: Sensing...", 0xFA60, 0x0000); // Màu Cam
            st7735_draw_string(lcd_dev, 4, 75, "SpO2 : Sensing...", 0xFA60, 0x0000);
        }

        // Hiển thị Nhiệt độ cơ thể đo được từ MLX90614
        if (last_valid_temp > 20.0f) {
            snprintf(str_buff, sizeof(str_buff), "Temp : %.2f C  ", last_valid_temp);
            st7735_draw_string(lcd_dev, 4, 100, str_buff, 0xFFFF, 0x0000); // Màu Trắng
        } else {
            st7735_draw_string(lcd_dev, 4, 100, "Temp : Measuring..", 0xFA60, 0x0000);
        }
    }
}



void app_main(void)
{
    ESP_LOGI(TAG, "Đang khởi tạo hệ thống...");
    
    g_data_mutex = xSemaphoreCreateMutex();
    if (g_data_mutex == NULL) {
        ESP_LOGE(TAG, "Khởi tạo vùng khóa Mutex thất bại!");
        return;
    }

    ppg_algorithm_init(); 

    // Cấu hình Bus I2C Master tổng
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_NUM_0,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, 
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));

    // Khởi tạo phần cứng đèn nền màn hình (GPIO 9)
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&led_conf);
    gpio_set_level(PIN_NUM_LED, 1); 

    // Khởi tạo bus SPI Master cho màn hình ST7735
    spi_bus_config_t spi_bus_cfg = {
        .miso_io_num = -1,                 
        .mosi_io_num = PIN_NUM_SDA,        
        .sclk_io_num = PIN_NUM_SCK,        
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 128 * 160 * 2   
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi_bus_cfg, SPI_DMA_CH_AUTO));

    // Khởi động Driver màn hình ST7735
    static st7735_dev_t lcd_device;
    esp_err_t lcd_ret = st7735_init(&lcd_device, LCD_HOST, PIN_NUM_SCK, PIN_NUM_SDA, PIN_NUM_CS, PIN_NUM_AO, PIN_NUM_RST);
    if (lcd_ret == ESP_OK) {
        ESP_LOGI(TAG, "Đã khởi tạo thành công driver đăng ký phần cứng màn hình ST7735!");
    } else {
        ESP_LOGE(TAG, "Lỗi đăng ký driver màn hình ST7735.");
        return;
    }

    // Cấu hình hoạt động cho MAX30102
    max30102_config_t sensor_config = {
        .mode = MAX30102_MODE_SPO2,                                   
        .adc_range = MAX30102_SPO2_ADC_RGE_4096nA,                    
        .sample_rate = MAX30102_SPO2_SR_100,                          
        .pulse_width = MAX30102_SPO2_PW_411us_18b,                    
        .led_current_red = MAX30102_LED_CURRENT_7MA,                  
        .led_current_ir = MAX30102_LED_CURRENT_7MA,                   
        .fifo_almost_full = MAX30102_FIFO_ALMOST_FULL_VAL_8, 
        .enable_fifo_rollover = true                                  
    };

    // Đăng ký khởi tạo Driver thiết bị MAX30102
    static max30102_dev_t max30102_device;
    esp_err_t ret;
    int retry_count = 0;
    const int max_retries = 100;

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

    // Khởi tạo và cấu hình MLX90614
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

    // Cấu hình chân điện áp ngắt GPIO cho chân INT
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,            
        .mode = GPIO_MODE_INPUT,                   
        .pin_bit_mask = (1ULL << MAX30102_INT_GPIO),
        .pull_down_en = 0,                         
        .pull_up_en = 1,                           
    };
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(MAX30102_INT_GPIO, max30102_gpio_isr_handler, NULL);

    // Đóng gói tham số cho Task nhiệt độ
    static temp_task_args_t temp_args;
    temp_args.max30102_dev = &max30102_device;
    temp_args.mlx90614_dev = mlx90614_device_handle;
    
    // khởi tạo các task freertos 
    xTaskCreate(max30102_processing_task, "max30102_task", 4096, &max30102_device, 10, &max30102_task_handle);    
    xTaskCreate(mlx90614_temperature_task, "mlx_temp_task", 4096, &temp_args, 2, NULL);
    xTaskCreate(lcd_display_task, "lcd_display_task", 4096, &lcd_device, 3, NULL);
}