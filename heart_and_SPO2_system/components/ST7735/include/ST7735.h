#ifndef ST7735_H
#define ST7735_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define ST7735_SWRESET 0x01  // Software Reset (Khởi động lại phần mềm)
#define ST7735_SLPOUT  0x11  // Sleep Out (Thoát chế độ ngủ)
#define ST7735_COLMOD  0x3A  // Interface Pixel Format (Thiết lập hệ màu)
#define ST7735_MADCTL  0x36  // Memory Data Access Control (Hướng quét RAM/Xoay màn hình)
#define ST7735_DISPON  0x29  // Display On (Kích hoạt hiển thị)
#define ST7735_CASET   0x2A  // Column Address Set (Định vị vùng cột X)
#define ST7735_RASET   0x2B  // Row Address Set (Định vị vùng hàng Y)
#define ST7735_RAMWR   0x2C  // Memory Write (Ghi dữ liệu pixel vào RAM)

// Cấu hình kích thước chuẩn hiển thị vật lý
#define ST7735_WIDTH   128
#define ST7735_HEIGHT  160

// Cấu trúc đóng gói tài nguyên phần cứng quản lý màn hình
typedef struct {
    spi_device_handle_t spi_handle;
    gpio_num_t ao_pin;   // Chân phân biệt Command/Data (Ký hiệu AO trên mạch của bạn)
    gpio_num_t rst_pin;  // Chân Reset cứng
    gpio_num_t cs_pin;   // Chân Chip Select ngoại vi SPI
} st7735_dev_t;
//Khởi tạo màn hình ST7735 và cấu hình chuỗi thanh ghi ban đầu
esp_err_t st7735_init(st7735_dev_t *dev, spi_host_device_t spi_host, 
                      gpio_num_t sck, gpio_num_t sda, gpio_num_t cs, 
                      gpio_num_t ao, gpio_num_t rst);

//Định vị vùng cửa sổ (Window) cần ghi dữ liệu pixel lên RAM màn hình
void st7735_set_window(st7735_dev_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);

// xóa toàn bộ màn hình về màu đen 
void st7735_clear_screen(st7735_dev_t *dev, uint16_t color);

// in một ký tự thuộc bảng mã assci 
void st7735_draw_char(st7735_dev_t *dev, uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color);

// in một chuỗi ký tự lên màn hình 
void st7735_draw_string(st7735_dev_t *dev, uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color);

#endif // ST7735_H