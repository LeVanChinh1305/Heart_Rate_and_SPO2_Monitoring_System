#include "MAX30102.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAX30102"; 
#define MAX30102_I2C_TIMEOUT_MS 1000

// ============================================
// HÀM HỖ TRỢ NỘI BỘ
// ============================================

static esp_err_t max30102_write_reg(max30102_dev_t *dev, uint8_t reg_addr, uint8_t data)
{
    uint8_t write_buf[2] = {reg_addr, data};
    return i2c_master_transmit(dev->i2c_dev_handle, write_buf, sizeof(write_buf), MAX30102_I2C_TIMEOUT_MS);
}

static esp_err_t max30102_read_reg(max30102_dev_t *dev, uint8_t reg_addr, uint8_t *data)
{
    if (dev == NULL || data == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = i2c_master_transmit_receive(
        dev->i2c_dev_handle, 
        &reg_addr, 1,           // Gửi 1 byte địa chỉ thanh ghi muốn đọc
        data, 1,                // Nhận về 1 byte dữ liệu tương ứng
        MAX30102_I2C_TIMEOUT_MS // Thời gian chờ (Timeout)
    );
    
    if (err != ESP_OK) {
        *data = 0x00; // Ép về 0x00 nếu lỗi để tránh giá trị rác (như 0xF7) làm hiểu lầm
    }
    return err;
}

static esp_err_t max30102_read_regs(max30102_dev_t *dev, uint8_t start_reg_addr, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(
        dev->i2c_dev_handle,
        &start_reg_addr, 1,      
        data, len,                 
        MAX30102_I2C_TIMEOUT_MS
    );
}

static esp_err_t max30102_write_reg_mask(max30102_dev_t *dev, uint8_t reg_addr, uint8_t mask, uint8_t data)
{
    if (dev == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t current_value = 0;
    
    esp_err_t err = max30102_read_reg(dev, reg_addr, &current_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02X: %s", reg_addr, esp_err_to_name(err));
        return err;
    }

    uint8_t new_value = (current_value & ~mask) | (data & mask); 
    return max30102_write_reg(dev, reg_addr, new_value);
}
// ============================================
// Private API
// ============================================
static esp_err_t max30102_config_led(max30102_dev_t *dev, uint8_t led1_current, uint8_t led2_current)
{
    if(dev == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = max30102_write_reg(dev, MAX30102_REG_LED1_PA, led1_current); 
    if(err != ESP_OK) return err;
    return max30102_write_reg(dev, MAX30102_REG_LED2_PA, led2_current); 
}

static esp_err_t max30102_config_spo2(max30102_dev_t *dev, uint8_t adc_range, uint8_t sample_rate, uint8_t pulse_width)
{
    if(dev == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t spo2_cfg_byte = (adc_range & 0x60) | (sample_rate & 0x1C) | (pulse_width & 0x03);
    return max30102_write_reg(dev, MAX30102_REG_SPO2_CONFIG, spo2_cfg_byte); 
}

static esp_err_t max30102_config_fifo(max30102_dev_t *dev, uint8_t sample_ave, bool enable_rollover, uint8_t fifo_almost_full)
{
    if(dev == NULL) return ESP_ERR_INVALID_ARG; 
    uint8_t fifo_config = (sample_ave & 0xE0) |       
                          (enable_rollover ? MAX30102_FIFO_ROLLOVER_EN : 0) |      
                          (fifo_almost_full & 0x0F);  
    return max30102_write_reg(dev, MAX30102_REG_FIFO_CONFIG, fifo_config); 
}

// ============================================
// PUBLIC APIs
// ============================================

esp_err_t max30102_reset(max30102_dev_t *dev)
{
    if(dev == NULL) return ESP_ERR_INVALID_ARG; 
    esp_err_t err = max30102_write_reg(dev, MAX30102_REG_MODE_CONFIG, MAX30102_MODE_RESET);
    if(err != ESP_OK) return err; 
    
    uint8_t mode_reg = 0; 
    int timeout = 10;
    do {
        vTaskDelay(pdMS_TO_TICKS(10));
        max30102_read_reg(dev, MAX30102_REG_MODE_CONFIG, &mode_reg);
        if ((mode_reg & MAX30102_MODE_RESET) == 0) break;
        timeout--;
    } while (timeout > 0);

    if (timeout == 0) {
        ESP_LOGE(TAG, "Reset thiết bị quá thời gian (Timeout)!");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t max30102_check_part_id(max30102_dev_t *dev, bool *is_match)
{
    if(dev == NULL || is_match == NULL) return ESP_ERR_INVALID_ARG; 
    uint8_t part_id = 0; 
    esp_err_t err = max30102_read_reg(dev, MAX30102_REG_PART_ID, &part_id);
    if (err != ESP_OK) return err;
    
    *is_match = (part_id == MAX30102_PART_ID_EXPECTED); 
    return ESP_OK;
}

esp_err_t max30102_clear_fifo(max30102_dev_t *dev)
{
    if(dev == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = max30102_write_reg(dev, MAX30102_REG_FIFO_WR_PTR, 0x00); 
    if(err != ESP_OK) return err;
    err = max30102_write_reg(dev, MAX30102_REG_OVF_COUNTER, 0x00); 
    if (err != ESP_OK) return err;
    return max30102_write_reg(dev, MAX30102_REG_FIFO_RD_PTR, 0x00); 
}

esp_err_t max30102_set_mode(max30102_dev_t *dev, uint8_t mode)
{
    if(dev == NULL) return ESP_ERR_INVALID_ARG;
    return max30102_write_reg_mask(dev, MAX30102_REG_MODE_CONFIG, 0x07 , mode); 
}

esp_err_t max30102_init(max30102_dev_t *dev, i2c_master_bus_handle_t i2c_bus_handle, const max30102_config_t *config)
{
    if(dev == NULL || config == NULL) return ESP_ERR_INVALID_ARG;
    memset(dev, 0, sizeof(max30102_dev_t));
    memcpy(&dev->config, config, sizeof(max30102_config_t));

    // 1. Đăng ký thiết bị I2C con vào Bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_I2C_ADDR,
        .scl_speed_hz = 400000, // Chạy Fast Mode 400kHz cho mượt
    };
    
    esp_err_t err = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev->i2c_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi thêm thiết bị vào I2C Bus: %s", esp_err_to_name(err));
        return err;
    }

    // 2. Kiểm tra Part ID
    bool part_match = false;
    // Gọi hàm kiểm tra part id nguyên bản ở trên
    esp_err_t id_err = max30102_check_part_id(dev, &part_match);

    // Đọc trực tiếp thanh ghi Part ID để ép in ra màn hình log
    uint8_t real_id = 0;
    max30102_read_reg(dev, MAX30102_REG_PART_ID, &real_id);
    ESP_LOGW("MAX30102_DEBUG", "==========> PART ID THỰC TẾ ĐỌC ĐƯỢC LÀ: 0x%02X <==========", real_id);

    // Nếu lỗi giao tiếp I2C hoàn toàn (không thấy thiết bị trên bus)
    if (id_err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi kết nối vật lý I2C hoặc sai cấu hình chân!");
        return id_err;
    }

    // Nếu kết nối được nhưng Part ID không phải là 0x15
    if (!part_match) {
        ESP_LOGW(TAG, "Cảnh báo: Chip phản hồi ID 0x%02X thay vì 0x15 mong đợi.", real_id);
        // Tạm thời cho qua (không return lỗi) để hệ thống tiếp tục khởi dịch và lấy mẫu test dữ liệu
    } else {
        ESP_LOGI(TAG, "Đã xác minh chính xác chip MAX30102 (ID: 0x15)!");
    }

    // 3. Reset cảm biến
    err = max30102_reset(dev);
    if(err != ESP_OK) return err;

    // 4. Cấu hình động từ tham số `config` truyền vào thay vì hardcode
    err |= max30102_config_led(dev, config->led_current_red, config->led_current_ir); 
    err |= max30102_config_spo2(dev, config->adc_range, config->sample_rate, config->pulse_width); 
    err |= max30102_config_fifo(dev, MAX30102_FIFO_AVE_4, config->enable_fifo_rollover, config->fifo_almost_full); 
    err |= max30102_set_mode(dev, config->mode); 
    if(err != ESP_OK){
        ESP_LOGE(TAG, "Lỗi cấu hình thanh ghi!");
        return err;
    }

    // 5. Kích hoạt Ngắt phần cứng nội bộ (Interrupt Enable)
    // Ghi vào thanh ghi MAX30102_REG_INTR_ENABLE_1 (0x02) để cho phép giật chân INT
    uint8_t int_enable_flags = 0x00;
    if (config->mode == MAX30102_MODE_SPO2 || config->mode == MAX30102_MODE_HR) {
        int_enable_flags |= MAX30102_INT_A_FULL;  // Bật ngắt FIFO Almost Full
        int_enable_flags |= MAX30102_INT_PPG_RDY; // Bật ngắt New Data Ready
    }
    
    err = max30102_write_reg(dev, MAX30102_REG_INTR_ENABLE_1, int_enable_flags);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Không thể kích hoạt thanh ghi ngắt!");
        return err;
    }

    // 6. CHỈNH SỬA MỚI: Đọc dọn cờ ngắt rác và xả sạch FIFO lúc khởi động
    uint8_t dummy_status1 = 0, dummy_status2 = 0;
    max30102_get_interrupt_status(dev, &dummy_status1, &dummy_status2);

    err = max30102_clear_fifo(dev);
    if(err != ESP_OK) return err;

    dev->is_initialized = true;
    return ESP_OK;
}

esp_err_t max30102_read_sample(max30102_dev_t *dev, max30102_sample_t *sample)
{
    if(dev == NULL || sample == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t raw_data[6]; 
    esp_err_t err = max30102_read_regs(dev, MAX30102_REG_FIFO_DATA, raw_data, sizeof(raw_data)); 
    if(err != ESP_OK) return err;
    
    sample->red = ((uint32_t)raw_data[0] << 16) | ((uint32_t)raw_data[1] << 8) | raw_data[2]; 
    sample->ir = ((uint32_t)raw_data[3] << 16) | ((uint32_t)raw_data[4] << 8) | raw_data[5]; 
    return ESP_OK;
}

esp_err_t max30102_read_samples(max30102_dev_t *dev, max30102_sample_t *samples, uint8_t count, uint8_t *samples_read)
{
    if(dev == NULL || samples == NULL || samples_read == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t available; 
    esp_err_t err = max30102_get_available_samples(dev, &available); 
    if (err != ESP_OK) return err;

    uint8_t to_read = (available < count) ? available : count; 
    *samples_read = to_read; 
    if(to_read == 0) return ESP_OK; 

    uint8_t raw_data[192]; 
    uint16_t total_bytes = to_read * 6; 
    err = max30102_read_regs(dev, MAX30102_REG_FIFO_DATA, raw_data, total_bytes); 
    if(err != ESP_OK) return err;
      
    for(uint8_t i = 0; i < to_read; i++){
        uint8_t idx = i * 6;
        samples[i].red = (((uint32_t)raw_data[idx] << 16) | ((uint32_t)raw_data[idx + 1] << 8) | raw_data[idx + 2]) & 0x03FFFF;
        samples[i].ir  = (((uint32_t)raw_data[idx + 3] << 16) | ((uint32_t)raw_data[idx + 4] << 8) | raw_data[idx + 5]) & 0x03FFFF;    
    }
    return ESP_OK;
}

esp_err_t max30102_get_available_samples(max30102_dev_t *dev, uint8_t *count)
{
    if(dev == NULL || count == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t a, b;
    esp_err_t err = max30102_read_reg(dev, MAX30102_REG_FIFO_WR_PTR, &a);
    if(err != ESP_OK) return err;
    err = max30102_read_reg(dev, MAX30102_REG_FIFO_RD_PTR, &b);
    if(err != ESP_OK) return err;
    
    if(a >= b ){
        *count = a - b; 
    }else{
        *count = (a + 32) - b; 
    }
    return ESP_OK;
}

esp_err_t max30102_read_temperature(max30102_dev_t *dev, float *temperature)
{
    if(dev == NULL || temperature == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err = max30102_write_reg(dev, MAX30102_REG_TEMP_CONFIG, MAX30102_TEMP_EN);
    if(dev == NULL || temperature == NULL) return ESP_ERR_INVALID_ARG; // Giữ logic cũ của bạn
    if(err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(35)); // 35ms là đủ cho chuyển đổi nhiệt độ theo datasheet
    
    int8_t temp_int;
    uint8_t temp_frac;
    err = max30102_read_reg(dev, MAX30102_REG_TEMP_INT, (uint8_t *)&temp_int);
    if(err != ESP_OK) return err;
    err = max30102_read_reg(dev, MAX30102_REG_TEMP_FRAC, &temp_frac);
    if(err != ESP_OK) return err;
    
    *temperature = (float)temp_int + ((float)(temp_frac & 0x0F) * 0.0625f);
    return ESP_OK;
}

esp_err_t max30102_set_led_current(max30102_dev_t *dev, float red_ma, float ir_ma)
{
    if(dev == NULL || red_ma < 0 || ir_ma < 0) return ESP_ERR_INVALID_ARG;
    uint8_t red_current = (uint8_t)(red_ma / 0.2f); 
    uint8_t ir_current = (uint8_t)(ir_ma / 0.2f); 
    return max30102_config_led(dev, red_current, ir_current); 
}

esp_err_t max30102_set_sample_rate(max30102_dev_t *dev, uint8_t sample_rate)
{
    if(dev == NULL) return ESP_ERR_INVALID_ARG; 
    return max30102_write_reg_mask(dev, MAX30102_REG_SPO2_CONFIG, 0x1C, sample_rate); 
}

esp_err_t max30102_shutdown(max30102_dev_t *dev, bool enable)
{
    if(dev == NULL) return ESP_ERR_INVALID_ARG;
    uint8_t shutdown_bit = enable ? MAX30102_MODE_SHDN : 0x00;
    return max30102_write_reg_mask(dev, MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SHDN, shutdown_bit); 
}

esp_err_t max30102_get_interrupt_status(max30102_dev_t *dev, uint8_t *status1, uint8_t *status2)
{
    if (dev == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err;
    if (status1 != NULL) {
        err = max30102_read_reg(dev, MAX30102_REG_INTR_STATUS_1, status1);
        if (err != ESP_OK) return err;
    }
    if (status2 != NULL) {
        err = max30102_read_reg(dev, MAX30102_REG_INTR_STATUS_2, status2);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t max30102_get_fifo_status(max30102_dev_t *dev, uint8_t *wr_ptr, uint8_t *rd_ptr, uint8_t *overflow)
{
    if (dev == NULL) return ESP_ERR_INVALID_ARG;
    esp_err_t err;
    if (wr_ptr) {
        err = max30102_read_reg(dev, MAX30102_REG_FIFO_WR_PTR, wr_ptr);
        if (err != ESP_OK) return err;
    }
    if (rd_ptr) {
        err = max30102_read_reg(dev, MAX30102_REG_FIFO_RD_PTR, rd_ptr);
        if (err != ESP_OK) return err;
    }
    if (overflow) {
        err = max30102_read_reg(dev, MAX30102_REG_OVF_COUNTER, overflow);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t max30102_configure_proximity(max30102_dev_t *dev, uint8_t threshold, float pilot_current)
{
    if (dev == NULL) return ESP_ERR_INVALID_ARG;
    
    esp_err_t err = max30102_write_reg_mask(dev, MAX30102_REG_INTR_ENABLE_1, MAX30102_INT_PROX, MAX30102_INT_PROX);
    if (err != ESP_OK) return err;
    
    err = max30102_write_reg(dev, 0x30, threshold); 
    if (err != ESP_OK) return err;
    
    uint8_t pilot_val = (uint8_t)(pilot_current / 0.2f);
    return max30102_write_reg(dev, MAX30102_REG_PILOT_PA, pilot_val);
}