#include "MLX90614.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char *TAG = "MLX90614_DRIVER";

static esp_err_t mlx90614_read_reg_16(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint16_t *out_data)
{
    uint8_t rx_buffer[3] = {0}; // Mảng nhận về 3 byte

    // Thực hiện chu kỳ Transmit (Gửi 1 byte địa chỉ) kết hợp Receive (Nhận về 3 byte) liên tục
    esp_err_t err = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, rx_buffer, 3, pdMS_TO_TICKS(100));
    
    if (err != ESP_OK) {
        // Không in log lỗi ở đây để tránh nghẽn Terminal nếu mất kết nối diện rộng, 
        // việc xử lý hiển thị lỗi sẽ để các hàm API bên ngoài đảm nhận.
        return err;
    }

    // SMBus truyền byte thấp trước (Data Low), sau đó tới byte cao (Data High).
    // Byte thứ 3 (rx_buffer[2]) là PEC (Packet Error Code - CRC8) ta có thể bỏ qua ở khoảng cách ngắn.
    *out_data = (rx_buffer[1] << 8) | rx_buffer[0];
    
    return ESP_OK;
}

esp_err_t mlx90614_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *out_dev_handle)
{
    if (bus_handle == NULL || out_dev_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Cấu hình thông số I2C Slave dành riêng cho MLX90614
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MLX90614_I2C_ADDR,
        .scl_speed_hz    = 100000, // Tốc độ 100KHz chuẩn giao tiếp SMBus của Melexis
    };

    // Đăng ký thiết bị vào quản lý của Bus Master tổng
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, out_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Lỗi đăng ký thiết bị vào I2C Bus: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Đã đăng ký Driver thành công với địa chỉ: 0x%02X", MLX90614_I2C_ADDR);
    return ESP_OK;
}

esp_err_t mlx90614_check_connection(i2c_master_dev_handle_t dev_handle)
{
    uint16_t id_part1 = 0;
    
    // Thử thách bắt tay bằng cách đọc thanh ghi ID thứ nhất trong bộ nhớ EEPROM
    esp_err_t err = mlx90614_read_reg_16(dev_handle, MLX90614_EEPROM_ID1, &id_part1);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Kiểm tra kết nối thất bại! Không tìm thấy thiết bị phản hồi trên đường I2C.");
        return err;
    }

    ESP_LOGI(TAG, "Kết nối THÀNH CÔNG! Đọc thử Serial ID1: 0x%04X", id_part1);
    return ESP_OK;
}

esp_err_t mlx90614_read_ambient(i2c_master_dev_handle_t dev_handle, float *ambient_temp)
{
    if (ambient_temp == NULL) return ESP_ERR_INVALID_ARG;

    uint16_t raw_data = 0;
    esp_err_t err = mlx90614_read_reg_16(dev_handle, MLX90614_RAM_TA, &raw_data);
    if (err != ESP_OK) {
        return err;
    }

    // Theo datasheet: Bit 15 (MSB) của dữ liệu thô báo lỗi cờ Flag tràn dữ liệu (nếu có)
    if (raw_data & 0x8000) {
        ESP_LOGW(TAG, "Cảnh báo: Phát hiện cờ bit lỗi tràn dữ liệu (Data Error Flag) tại RAM Ta.");
    }

    // Công thức tính toán của nhà sản xuất:
    // T = Raw * 0.02 (Kết quả trả về đơn vị độ Kelvin)
    // Chuyển sang độ C: T(°C) = T(°K) - 273.15
    *ambient_temp = ((float)raw_data * 0.02f) - 273.15f;
    
    return ESP_OK;
}

esp_err_t mlx90614_read_object(i2c_master_dev_handle_t dev_handle, float *object_temp)
{
    if (object_temp == NULL) return ESP_ERR_INVALID_ARG;

    uint16_t raw_data = 0;
    esp_err_t err = mlx90614_read_reg_16(dev_handle, MLX90614_RAM_TOBJ1, &raw_data);
    if (err != ESP_OK) {
        return err;
    }

    // Thực hiện chuyển đổi toán học tương tự từ giá trị thô sang độ C
    *object_temp = ((float)raw_data * 0.02f) - 273.15f;
    
    return ESP_OK;
}