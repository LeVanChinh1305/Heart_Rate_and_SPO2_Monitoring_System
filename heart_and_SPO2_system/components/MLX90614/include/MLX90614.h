#ifndef MLX90614_H
#define MLX90614_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#define MLX90614_I2C_ADDR       0x5A    // Địa chỉ I2C mặc định (7-bit) của MLX90614


// Địa chỉ các thanh ghi vùng RAM (Đọc giá trị nhiệt độ)

#define MLX90614_RAM_TA         0x06    // Nhiệt độ môi trường (Ambient Temperature)
#define MLX90614_RAM_TOBJ1      0x07    // Nhiệt độ vật thể vùng 1 (Object 1 Temperature)
#define MLX90614_RAM_TOBJ2      0x08    // Nhiệt độ vật thể vùng 2 (Chỉ có ở dòng cảm biến dual-zone)

// Địa chỉ các thanh ghi vùng EEPROM (Đọc thông tin cấu hình / ID)
#define MLX90614_EEPROM_ID1     0x1C    // Serial ID phần 1
#define MLX90614_EEPROM_ID2     0x1D    // Serial ID phần 2
#define MLX90614_EEPROM_ID3     0x1E    // Serial ID phần 3
#define MLX90614_EEPROM_ID4     0x1F    // Serial ID phần 4


// Khai báo các hàm API chức năng của Driver

//Đăng ký thiết bị MLX90614 vào I2C Master Bus tổng của ESP32.
esp_err_t mlx90614_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *out_dev_handle);

//Kiểm tra kết nối vật lý với cảm biến bằng cách bắt tay đọc thử Serial ID.
esp_err_t mlx90614_check_connection(i2c_master_dev_handle_t dev_handle);

//ọc nhiệt độ môi trường xung quanh cảm biến (Ambient Temperature).
esp_err_t mlx90614_read_ambient(i2c_master_dev_handle_t dev_handle, float *ambient_temp);

//Đọc nhiệt độ của vật thể / bề mặt da người (Object Temperature).
esp_err_t mlx90614_read_object(i2c_master_dev_handle_t dev_handle, float *object_temp);

#endif // MLX90614_H