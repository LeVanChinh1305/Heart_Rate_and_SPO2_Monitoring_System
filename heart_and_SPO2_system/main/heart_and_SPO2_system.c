#include <stdio.h>                   //thư viện chuẩn C cho các hàm vào ra cơ bản như printf, snprintf
#include <string.h>                  //thư viện chuẩn C cho các hàm xử lý chuỗi như memset, memcpy
#include "freertos/FreeRTOS.h"       //thư viện FreeRTOS cho các hàm quản lý hệ điều hành thời gian thực như tạo task, delay, mutex
#include "freertos/task.h"           //thư viện FreeRTOS cho các hàm quản lý task như xTaskCreate, vTaskDelay
#include "freertos/queue.h"          //thư viện FreeRTOS cho các hàm quản lý hàng đợi như xQueueCreate, xQueueSend, xQueueReceive
#include "freertos/semphr.h"         //thư viện FreeRTOS cho các hàm quản lý semaphore như xSemaphoreCreateMutex, xSemaphoreTake, xSemaphoreGive
#include "freertos/event_groups.h"   //thư viện FreeRTOS cho các hàm quản lý nhóm sự kiện như xEventGroupCreate, xEventGroupSetBits, xEventGroupWaitBits
#include "driver/gpio.h"             //thư viện ESP-IDF cho các hàm quản lý GPIO như gpio_config, gpio_set_level, gpio_install_isr_service
#include "driver/i2c_master.h"       //thư viện ESP-IDF cho các hàm quản lý I2C Master như i2c_master_init, i2c_master_read, i2c_master_write
#include "driver/spi_master.h"       //thư viện ESP-IDF cho các hàm quản lý SPI Master như spi_bus_initialize, spi_device_transmit
#include "esp_log.h"                 //thư viện ESP-IDF cho các hàm quản lý log như ESP_LOGI, ESP_LOGE, ESP_LOGW
#include "esp_err.h"                 //thư viện ESP-IDF cho các hàm quản lý lỗi như esp_err_to_name, ESP_ERROR_CHECK
#include "nvs_flash.h"               //thư viện ESP-IDF cho các hàm quản lý bộ nhớ flash không bay hơi như nvs_flash_init, nvs_flash_erase
#include "MAX30102.h"                // các driver của các components liên quan 
#include "MLX90614.h"
#include "ppg_algorithm.h"
#include "st7735.h"           
#include "WIFI.h"
#include "my_mqtt.h"
#include "button1.h"                 

static const char *TAG = "MAIN_APP"; // Tag dùng cho log để dễ dàng phân biệt nguồn log trong ESP-IDF Monitor

#define I2C_MASTER_SDA_IO           GPIO_NUM_6    
#define I2C_MASTER_SCL_IO           GPIO_NUM_7   
#define MAX30102_INT_GPIO           GPIO_NUM_4   

#define LCD_HOST                    SPI2_HOST
#define PIN_NUM_SCK                 GPIO_NUM_0
#define PIN_NUM_SDA                 GPIO_NUM_1  
#define PIN_NUM_RST                 GPIO_NUM_2
#define PIN_NUM_AO                  GPIO_NUM_3  
#define PIN_NUM_CS                  GPIO_NUM_8
#define PIN_NUM_LED                 GPIO_NUM_9  

typedef struct {
    float heart_rate;
    float spo2;
    float body_temp;
    bool ppg_valid; // Cờ hiệu dữ liệu PPG có hợp lệ hay không (đủ tín hiệu tốt để hiển thị và gửi đi)
} health_data_t;

typedef struct {
    max30102_dev_t *max30102_dev; // Con trỏ đến cấu trúc thiết bị MAX30102 để chia sẻ giữa các Task
    i2c_master_dev_handle_t mlx90614_dev; // Handle thiết bị MLX90614, có thể NULL nếu cảm biến này không được kết nối hoặc khởi tạo thất bại
} temp_task_args_t; // cấu trúc task giám sát nhiệt độ tổng hợp

static health_data_t g_health_data = {0}; // biến cấu trúc toàn cục lưu trữ dữ liệu sức khỏe hiện tại
static SemaphoreHandle_t g_data_mutex = NULL; // Mutex bảo vệ truy cập đồng thời vào g_health_data giữa các Task 
static TaskHandle_t max30102_task_handle = NULL; //Lưu handle của Task đọc cảm biến MAX30102.
SemaphoreHandle_t i2c_bus_mutex = NULL; // Mutex bảo vệ Bus I2C dùng chung

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
    
    uint32_t non_valid_counter = 0; 

    ESP_LOGI(TAG, "Task xử lý MAX30102 bắt đầu hoạt động...");

    while (1) {
        // Đọc tín hiệu ngắt phần cứng từ MAX30102
        uint32_t is_notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        
        // Kiểm tra bit sự kiện FreeRTOS xem hệ thống hiện tại đang cho phép ĐO hay đang THÁO MÁY
        EventBits_t current_bits = xEventGroupGetBits(g_device_event_group);
        bool is_wearing = (current_bits & BIT_STATUS_WEARING) ? true : false;

        // TRƯỜNG HỢP 1: Không có tín hiệu ngắt (Timeout) HOẶC Người dùng chủ động chọn THÁO MÁY (từ nút bấm/web)
        if (is_notified == 0 || !is_wearing) {
            
            // Xóa sạch cờ ngắt cũ để bảo vệ bus I2C và tránh treo chân INT phần cứng
            if (xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(50)) == pdTRUE) { 
                uint8_t s1 = 0, s2 = 0; 
                max30102_get_interrupt_status(max30102_dev, &s1, &s2); 
                xSemaphoreGive(i2c_bus_mutex); 
            } 
            
            // Đưa thông số cấu trúc toàn cục về 0
            if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_health_data.heart_rate = 0.0f;
                g_health_data.spo2 = 0.0f;
                g_health_data.ppg_valid = false; 
                xSemaphoreGive(g_data_mutex); 
            }
            
            // Nếu mất ngắt phần cứng quá lâu (>1s) khi cấu hình vẫn đang báo đeo máy -> Tự động kích hoạt cờ THÁO MÁY
            if (is_notified == 0 && is_wearing) {
                ESP_LOGW(TAG, "Timeout ngắt! Cảm biến không nhận diện bề mặt tiếp xúc. Tự động chuyển sang chế độ THÁO MÁY.");
                xEventGroupClearBits(g_device_event_group, BIT_STATUS_WEARING);
                xEventGroupSetBits(g_device_event_group, BIT_STATUS_NOT_WEARING);
            }

            vTaskDelay(pdMS_TO_TICKS(100)); // Delay nhỏ giảm tải CPU khi đang ở trạng thái nghỉ
            continue; 
        }

        // TRƯỜNG HỢP 2: Thiết bị đang đeo (WEARING) và có dữ liệu ngắt sẵn sàng từ cảm biến
        bool bus_clear = false;
        if (xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (max30102_get_interrupt_status(max30102_dev, &status1, &status2) == ESP_OK) {
                if (status1 & (MAX30102_INT_A_FULL | MAX30102_INT_PPG_RDY)) {
                    esp_err_t err = max30102_read_samples(max30102_dev, sample_buffer, 32, &samples_read);
                    if (err == ESP_OK && samples_read > 0) {
                        bus_clear = true; 
                    }
                }
            }
            xSemaphoreGive(i2c_bus_mutex); 
        }

        // Xử lý dữ liệu qua thuật toán phân tích PPG
        if (bus_clear) {
            for (int i = 0; i < samples_read; i++) {
                
                // Gửi dữ liệu sóng thô sau lọc DC liên tục lên đồ thị Web
                mqtt_publish_raw_single((int32_t)sample_buffer[i].ir);

                // Đưa vào thuật toán xử lý phân tích nhịp tim & SpO2
                if (ppg_algorithm_process_sample(sample_buffer[i].red, sample_buffer[i].ir, &ppg_result)) {
                    
                    if (ppg_result.valid) {
                        // Người dùng đang đo bình thường và mạch ổn định
                        non_valid_counter = 0; 

                        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                            g_health_data.heart_rate = ppg_result.heart_rate;
                            g_health_data.spo2 = ppg_result.spo2;
                            g_health_data.ppg_valid = ppg_result.valid;
                            xSemaphoreGive(g_data_mutex); 
                        }

                        // Đẩy dữ liệu sinh hiệu đóng gói JSON lên MQTT Broker
                        float current_temp = 0.0f;
                        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                            current_temp = g_health_data.body_temp;
                            xSemaphoreGive(g_data_mutex);
                        }
                        mqtt_pulish_data(ppg_result.heart_rate, ppg_result.spo2, current_temp);

                    } else {
                        // KỊCH BẢN NGUY KỊCH: ĐANG ĐEO MÁY NHƯNG PHÁT HIỆN NGỪNG THỞ / MẤT MẠCH (Flatline)
                        non_valid_counter++;

                        // Tín hiệu phẳng không đổi liên tục trong khoảng 100 mẫu (~1 giây)
                        if (non_valid_counter >= 100) {
                            non_valid_counter = 0; 
                            
                            ESP_LOGE("HEALTH_ALERT", "CẢNH BÁO NGUY KỊCH: Mất tuần hoàn/Ngừng thở trên cơ thể!");

                            // Ép giá trị về 0 lập tức bảo vệ an toàn
                            if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                                g_health_data.heart_rate = 0.0f;
                                g_health_data.spo2 = 0.0f;
                                g_health_data.ppg_valid = false; 
                                xSemaphoreGive(g_data_mutex); 
                            }

                            // Đồng bộ đẩy dữ liệu khẩn cấp về 0.0f lên Web Server
                            float current_temp = 0.0f;
                            if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                                current_temp = g_health_data.body_temp;
                                xSemaphoreGive(g_data_mutex);
                            }
                            mqtt_pulish_data(0.0f, 0.0f, current_temp);                        
                        }
                    }
                }
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
        vTaskDelay(pdMS_TO_TICKS(5000)); // Đọc định kỳ mỗi 5 giây

        // Kiểm tra xem thiết bị có đang ở trạng thái đeo máy hay không
        EventBits_t bits = xEventGroupGetBits(g_device_event_group);
        if (!(bits & BIT_STATUS_WEARING)) {
            // Nếu đang tháo máy, bỏ qua chu kỳ đọc nhiệt độ hồng ngoại để tối ưu hệ thống
            continue; 
        }

        // Đọc nhiệt độ bo mạch nội bộ của MAX30102
        if (xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (max30102_read_temperature(args->max30102_dev, &max30102_temp) == ESP_OK) {
                ESP_LOGI("TEMP_MONITOR", "Nhiệt độ bo mạch MAX30102: %.2f °C", max30102_temp);
            }
            xSemaphoreGive(i2c_bus_mutex);
        }

        // Đọc cảm biến nhiệt độ không tiếp xúc MLX90614
        if (args->mlx90614_dev != NULL) {
            esp_err_t err_ta = ESP_FAIL;
            esp_err_t err_to = ESP_FAIL;

            if (xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                err_ta = mlx90614_read_ambient(args->mlx90614_dev, &body_temp_ambient);
                err_to = mlx90614_read_object(args->mlx90614_dev, &body_temp_object);
                xSemaphoreGive(i2c_bus_mutex);
            }

            if (err_ta == ESP_OK && err_to == ESP_OK) {
                ESP_LOGI("TEMP_MONITOR", "MLX90614 -> Môi trường: %.2f °C | Cơ thể: %.2f °C", body_temp_ambient, body_temp_object);
                
                if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    g_health_data.body_temp = body_temp_object;
                    xSemaphoreGive(g_data_mutex);
                }

                // Gửi cập nhật thông số nhiệt độ mới đồng bộ lên mạng
                float current_heart_rate = 0.0f;
                float current_spo2 = 0.0f;

                if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    current_heart_rate = g_health_data.heart_rate;
                    current_spo2 = g_health_data.spo2;
                    xSemaphoreGive(g_data_mutex);
                }

                mqtt_pulish_data(current_heart_rate, current_spo2, body_temp_object);

            } else {
                ESP_LOGE("TEMP_MONITOR", "Lỗi đường truyền bus I2C với MLX90614!");
            }
        }
    }
}

void lcd_display_task(void *pvParameters)
{
    st7735_dev_t *lcd_dev = (st7735_dev_t *)pvParameters;
    health_data_t local_data;
    char str_buff[32];

    st7735_clear_screen(lcd_dev, 0x0000); 
    st7735_draw_string(lcd_dev, 8, 10, "SMART WRISTBAND", 0x07FF, 0x0000); 
    st7735_draw_string(lcd_dev, 0, 28, "----------------", 0xFFFF, 0x0000); 

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(300)); 

        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            local_data = g_health_data;
            xSemaphoreGive(g_data_mutex);
        }

        // Lấy thông tin trạng thái thiết bị thời gian thực từ Event Group
        EventBits_t bits = xEventGroupGetBits(g_device_event_group);

        if (bits & BIT_STATUS_WEARING) {
            // Chế độ 1: Đang đeo máy và xử lý dữ liệu
            if (local_data.ppg_valid) {
                snprintf(str_buff, sizeof(str_buff), "Pulse: %.1f BPM ", local_data.heart_rate);
                st7735_draw_string(lcd_dev, 4, 50, str_buff, 0x07E0, 0x0000); // Màu Xanh lá
                
                snprintf(str_buff, sizeof(str_buff), "SpO2 : %.1f %%  ", local_data.spo2);
                st7735_draw_string(lcd_dev, 4, 75, str_buff, 0x07E0, 0x0000); 
            } else {
                // Tín hiệu phẳng khi đang cấu hình đo -> Báo động nguy kịch ngay trên màn hình nền
                if (local_data.heart_rate == 0.0f && local_data.spo2 == 0.0f) {
                    st7735_draw_string(lcd_dev, 4, 50, "PULSE: EMERGENCY", 0xF800, 0x0000); // Màu Đỏ
                    st7735_draw_string(lcd_dev, 4, 75, "SPO2 : CRITICAL ", 0xF800, 0x0000);
                } else {
                    st7735_draw_string(lcd_dev, 4, 50, "Pulse: Sensing...", 0xFA60, 0x0000); // Màu Cam
                    st7735_draw_string(lcd_dev, 4, 75, "SpO2 : Sensing...", 0xFA60, 0x0000);
                }
            }

            if (local_data.body_temp > 20.0f) {
                snprintf(str_buff, sizeof(str_buff), "Temp : %.2f C    ", local_data.body_temp);
                st7735_draw_string(lcd_dev, 4, 100, str_buff, 0xFFFF, 0x0000); 
            } else {
                st7735_draw_string(lcd_dev, 4, 100, "Temp : Measuring..", 0xFA60, 0x0000);
            }
        } 
        else {
            // Chế độ 2: Trạng thái THÁO MÁY (NOT WEARING) -> Xóa màn hình hiển thị text chờ
            st7735_draw_string(lcd_dev, 4, 50, "STATUS: STANDBY ", 0x001F, 0x0000); // Màu Xanh Dương
            st7735_draw_string(lcd_dev, 4, 75, "Device Removed  ", 0xFFFF, 0x0000);
            st7735_draw_string(lcd_dev, 4, 100, "Push Button to- ", 0x7BEF, 0x0000);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Đang khởi tạo toàn bộ hệ thống...");

    // [BƯỚC 1]: Khởi tạo dịch vụ ngắt cứng hệ thống GPIO ISR Service đầu tiên
    gpio_install_isr_service(0);

    // [BƯỚC 2]: Khởi tạo Driver Nút bấm vật lý (Tạo g_device_event_group bên trong trước mọi thứ)
    button1_hardware_init(); 

    // [BƯỚC 3]: Tạo các thành phần đồng bộ khóa Mutex bảo vệ Bus tài nguyên
    i2c_bus_mutex = xSemaphoreCreateMutex();
    g_data_mutex = xSemaphoreCreateMutex();
    if (i2c_bus_mutex == NULL || g_data_mutex == NULL) {
        ESP_LOGE(TAG, "Không thể tạo Mutex quản lý đồng bộ!");
        return;
    }

    // Khởi tạo lưu trữ NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Khởi động kết nối Wi-Fi & Giao thức mạng MQTT Driver ngầm
    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_ERROR_CHECK(mqtt_start_init()); 
    vTaskDelay(pdMS_TO_TICKS(500));

    ppg_algorithm_init(); 

    // Cấu hình mạng truyền thông phần cứng I2C Bus Master
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

    // Cấu hình LED nền cho màn hình LCD ST7735
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&led_conf);
    gpio_set_level(PIN_NUM_LED, 1); 

    // Khởi tạo Bus SPI và Driver màn hình LCD ST7735
    spi_bus_config_t spi_bus_cfg = {
        .miso_io_num = -1,                                 
        .mosi_io_num = PIN_NUM_SDA,        
        .sclk_io_num = PIN_NUM_SCK,        
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 128 * 160 * 2   
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &spi_bus_cfg, SPI_DMA_CH_AUTO));

    static st7735_dev_t lcd_device;
    ESP_ERROR_CHECK(st7735_init(&lcd_device, LCD_HOST, PIN_NUM_SCK, PIN_NUM_SDA, PIN_NUM_CS, PIN_NUM_AO, PIN_NUM_RST));
    ESP_LOGI(TAG, "Khởi tạo thành công phần cứng màn hình màu ST7735!");

    // Cấu hình tham số cảm biến MAX30102
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

    static max30102_dev_t max30102_device;
    int retry_count = 0;
    const int max_retries = 5;

    while (retry_count < max_retries) {
        if (xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            ret = max30102_init(&max30102_device, bus_handle, &sensor_config);
            xSemaphoreGive(i2c_bus_mutex);
        }
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Khởi tạo cảm biến sinh hiệu MAX30102 thành công!");
            break;
        }
        retry_count++;
        vTaskDelay(pdMS_TO_TICKS(500)); 
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi nghiêm trọng phần cứng MAX30102! Khóa bo mạch.");
        return; 
    }

    // Khởi tạo cảm biến nhiệt độ MLX90614 hồng ngoại dùng chung Bus I2C
    i2c_master_dev_handle_t mlx90614_device_handle = NULL;
    bool mlx_ok = false;
    
    if (xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (mlx90614_init(bus_handle, &mlx90614_device_handle) == ESP_OK) {
            if (mlx90614_check_connection(mlx90614_device_handle) == ESP_OK) {
                mlx_ok = true;
            }
        }
        xSemaphoreGive(i2c_bus_mutex);
    }

    if (mlx_ok) {
        ESP_LOGI(TAG, "Kết nối thành công cảm biến nhiệt độ không tiếp xúc MLX90614!");
    } else {
        ESP_LOGE(TAG, "Không tìm thấy MLX90614 trên Bus I2C. Tiếp tục cô lập linh kiện này...");
        mlx90614_device_handle = NULL;
    }

    // Đăng ký chân ngắt vật lý INT của MAX30102 lên bộ dịch vụ ISR ngắt cạnh xuống
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_NEGEDGE,            
        .mode = GPIO_MODE_INPUT,                                   
        .pin_bit_mask = (1ULL << MAX30102_INT_GPIO),
        .pull_down_en = 0,                                          
        .pull_up_en = 1,                                           
    };
    gpio_config(&io_conf);
    gpio_isr_handler_add(MAX30102_INT_GPIO, max30102_gpio_isr_handler, NULL);

    // Đóng gói cấu trúc đối số chia sẻ Task
    static temp_task_args_t temp_args;
    temp_args.max30102_dev = &max30102_device;
    temp_args.mlx90614_dev = mlx90614_device_handle;
    
    // [BƯỚC 4]: Kích hoạt phân phối các Task xử lý FreeRTOS vận hành lõi
    xTaskCreate(max30102_processing_task, "max30102_task", 4096, &max30102_device, 10, &max30102_task_handle);    
    xTaskCreate(mlx90614_temperature_task, "mlx_temp_task", 4096, &temp_args, 2, NULL);
    xTaskCreate(lcd_display_task, "lcd_display_task", 4096, &lcd_device, 3, NULL);
}