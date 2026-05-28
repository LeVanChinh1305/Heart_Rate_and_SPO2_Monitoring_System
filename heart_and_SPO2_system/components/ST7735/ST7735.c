#include "ST7735.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

// Bộ Font chữ 8x16 bitmap tối giản cho các ký tự ASCII cơ bản (Từ ' ' đến '~')
// Mỗi ký tự gồm 16 dòng, mỗi dòng đại diện bằng 1 byte (8 bit pixel)
static const uint8_t font_8x16_basic[95][16] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // Space ' '
    [45-32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // Dấu trừ '-'
    [46-32] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x00,0x00}, // Dấu chấm '.'
    [58-32] = {0x00,0x00,0x00,0x00,0x0c,0x0c,0x00,0x00,0x00,0x00,0x0c,0x0c,0x00,0x00,0x00,0x00}, // Dấu hai chấm ':'
    [37-32] = {0x00,0x00,0x46,0x4c,0x3a,0x32,0x16,0x0c,0x18,0x34,0x2c,0x5c,0x62,0x00,0x00,0x00}, // Dấu phần trăm '%'
    // Định nghĩa các ký tự số từ '0' đến '9'
    [48-32] = {0x00,0x00,0x1c,0x22,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x22,0x1c,0x00,0x00,0x00}, // '0'
    [49-32] = {0x00,0x00,0x08,0x1c,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x1c,0x00,0x00,0x00}, // '1'
    [50-32] = {0x00,0x00,0x1c,0x22,0x42,0x02,0x02,0x04,0x08,0x10,0x20,0x42,0x7e,0x00,0x00,0x00}, // '2'
    [51-32] = {0x00,0x00,0x1c,0x22,0x42,0x02,0x02,0x1c,0x02,0x02,0x42,0x22,0x1c,0x00,0x00,0x00}, // '3'
    [52-32] = {0x00,0x00,0x04,0x0c,0x14,0x24,0x44,0x44,0x7e,0x04,0x04,0x04,0x1e,0x00,0x00,0x00}, // '4'
    [53-32] = {0x00,0x00,0x7e,0x40,0x40,0x40,0x7c,0x02,0x02,0x02,0x42,0x22,0x1c,0x00,0x00,0x00}, // '5'
    [54-32] = {0x00,0x00,0x1c,0x22,0x40,0x40,0x7c,0x42,0x42,0x42,0x42,0x22,0x1c,0x00,0x00,0x00}, // '6'
    [55-32] = {0x00,0x00,0x7e,0x42,0x02,0x02,0x04,0x04,0x08,0x08,0x10,0x10,0x10,0x00,0x00,0x00}, // '7'
    [56-32] = {0x00,0x00,0x1c,0x22,0x42,0x22,0x1c,0x22,0x42,0x42,0x42,0x22,0x1c,0x00,0x00,0x00}, // '8'
    [57-32] = {0x00,0x00,0x1c,0x22,0x42,0x42,0x42,0x3e,0x02,0x02,0x42,0x22,0x1c,0x00,0x00,0x00}, // '9'
    // Định nghĩa một số ký tự chữ cái in hoa cơ bản phục vụ giao diện sức khỏe
    ['A'-32] = {0x00,0x00,0x10,0x18,0x28,0x28,0x44,0x44,0x7c,0x44,0x44,0x44,0x44,0x00,0x00,0x00},
    ['B'-32] = {0x00,0x00,0x7c,0x22,0x22,0x22,0x3c,0x22,0x22,0x22,0x22,0x22,0x7c,0x00,0x00,0x00},
    ['C'-32] = {0x00,0x00,0x3c,0x42,0x42,0x40,0x40,0x40,0x40,0x40,0x42,0x42,0x3c,0x00,0x00,0x00},
    ['M'-32] = {0x00,0x00,0x42,0x62,0x72,0x52,0x4a,0x4a,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00},
    ['P'-32] = {0x00,0x00,0x7c,0x22,0x22,0x22,0x3c,0x40,0x40,0x40,0x40,0x40,0x40,0x00,0x00,0x00},
    ['R'-32] = {0x00,0x00,0x7c,0x22,0x22,0x22,0x3c,0x28,0x24,0x44,0x44,0x44,0x44,0x00,0x00,0x00},
    ['S'-32] = {0x00,0x00,0x3c,0x42,0x42,0x40,0x3c,0x02,0x02,0x02,0x42,0x22,0x1c,0x00,0x00,0x00},
    ['T'-32] = {0x00,0x00,0x7e,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00},
    ['W'-32] = {0x00,0x00,0x42,0x42,0x42,0x42,0x4a,0x4a,0x52,0x52,0x62,0x22,0x22,0x00,0x00,0x00},
};

// Hàm gửi LỆNH (Command) qua bus SPI: Kéo chân AO xuống mức THẤP (0)
static void st7735_write_cmd(st7735_dev_t *dev, uint8_t cmd) {
    gpio_set_level(dev->ao_pin, 0); 
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;         
    t.tx_buffer = &cmd;   
    spi_device_polling_transmit(dev->spi_handle, &t); // Truyền đồng bộ Polling tối ưu tốc độ
}

// Hàm gửi DỮ LIỆU (Data) 8-bit qua bus SPI: Kéo chân AO lên mức CAO (1)
static void st7735_write_data_8(st7735_dev_t *dev, uint8_t data) {
    gpio_set_level(dev->ao_pin, 1); 
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &data;
    spi_device_polling_transmit(dev->spi_handle, &t);
}

// Hàm gửi DỮ LIỆU 16-bit (Mã màu đồ họa RGB565): Tách thành 2 byte liên tiếp gửi đi
static void st7735_write_data_16(st7735_dev_t *dev, uint16_t data) {
    uint8_t byte_arr[2];
    byte_arr[0] = (data >> 8) & 0xFF; // Byte cao (MSB) chứa dữ liệu kênh màu Đỏ - Xanh lá
    byte_arr[1] = data & 0xFF;        // Byte thấp (LSB) chứa dữ liệu kênh màu Xanh lá - Xanh dương
    
    gpio_set_level(dev->ao_pin, 1); 
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 16;                    // Thiết lập độ dài khung truyền 16-bit
    t.tx_buffer = byte_arr;
    spi_device_polling_transmit(dev->spi_handle, &t);
}

// Hàm khởi tạo và nạp chuỗi lệnh cấu hình Register cho ST7735
esp_err_t st7735_init(st7735_dev_t *dev, spi_host_device_t spi_host, 
                      gpio_num_t sck, gpio_num_t sda, gpio_num_t cs, 
                      gpio_num_t ao, gpio_num_t rst) {
    dev->ao_pin = ao;
    dev->rst_pin = rst;
    dev->cs_pin = cs;

    // 1. Khởi tạo chức năng OutPut cho các chân GPIO điều khiển độc lập
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ao) | (1ULL << rst) | (1ULL << cs),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    gpio_config(&io_conf);

    // 2. Định nghĩa cấu hình thiết bị SPI gắn vào bus
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 26 * 1000 * 1000, // Tốc độ xung Clock 26 MHz mượt mà cho ST7735
        .mode = 0,                          // Chuẩn SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = cs,                 // Điều khiển tự động chân CS
        .queue_size = 7,
    };
    esp_err_t ret = spi_bus_add_device(spi_host, &dev_cfg, &dev->spi_handle);
    if (ret != ESP_OK) return ret;

    // 3. Thực hiện chu kỳ Reset cứng màn hình (Hardware Reset)
    gpio_set_level(dev->rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(dev->rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // 4. Thực thi nạp chuỗi lệnh khởi động từ tài liệu kỹ thuật datasheet
    st7735_write_cmd(dev, ST7735_SWRESET); // Reset mềm phần mềm cấu hình chip
    vTaskDelay(pdMS_TO_TICKS(120));

    st7735_write_cmd(dev, ST7735_SLPOUT);  // Đưa màn hình thoát khỏi trạng thái Sleep Mode
    vTaskDelay(pdMS_TO_TICKS(120));

    st7735_write_cmd(dev, ST7735_COLMOD);  // Thiết lập hệ màu
    st7735_write_data_8(dev, 0x05);        // Cấu hình định dạng màu 16-bit RGB565

    st7735_write_cmd(dev, ST7735_MADCTL);  // Điều khiển cấu trúc hướng hiển thị
    st7735_write_data_8(dev, 0xC8);        // Hướng thiết lập chuẩn cho màn hình dạng đứng đứng

    st7735_write_cmd(dev, ST7735_DISPON);  // Kích hoạt hiển thị màn hình (Display ON)
    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

// Định vị tọa độ vùng vẽ cụ thể giúp tối ưu bộ đệm màn hình (Partial Refresh Window)
void st7735_set_window(st7735_dev_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    st7735_write_cmd(dev, ST7735_CASET); // Cấu hình dải cột ngang (X)
    st7735_write_data_16(dev, x1);
    st7735_write_data_16(dev, x2);

    st7735_write_cmd(dev, ST7735_RASET); // Cấu hình dải hàng dọc (Y)
    st7735_write_data_16(dev, y1);
    st7735_write_data_16(dev, y2);

    st7735_write_cmd(dev, ST7735_RAMWR); // Bật chế độ sẵn sàng nhận pixel đẩy vào bộ nhớ Gram
}

// Xóa trắng màn hình nhanh bằng một màu nền tĩnh
void st7735_clear_screen(st7735_dev_t *dev, uint16_t color) {
    st7735_set_window(dev, 0, 0, ST7735_WIDTH - 1, ST7735_HEIGHT - 1);
    for (int i = 0; i < ST7735_WIDTH * ST7735_HEIGHT; i++) {
        st7735_write_data_16(dev, color);
    }
}

// Quét mảng bitmap vẽ một ký tự cụ thể lên màn hình
void st7735_draw_char(st7735_dev_t *dev, uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg_color) {
    if (ch < ' ' || ch > '~') ch = ' '; // Chuyển đổi các ký tự nằm ngoài bảng mã font thành Space
    uint8_t c_index = ch - ' ';

    st7735_set_window(dev, x, y, x + 7, y + 15); // Tạo cửa sổ kích thước đúng bằng ký tự 8x16

    for (int row = 0; row < 16; row++) {
        uint8_t bits = font_8x16_basic[c_index][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                st7735_write_data_16(dev, color);    // Pixel bật: Vẽ màu chữ
            } else {
                st7735_write_data_16(dev, bg_color); // Pixel tắt: Vẽ màu nền
            }
        }
    }
}

// Vẽ một chuỗi văn bản hoàn chỉnh bằng cách lặp tiến bước vẽ các ký tự
void st7735_draw_string(st7735_dev_t *dev, uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color) {
    while (*str) {
        if (x + 8 > ST7735_WIDTH) { // Tự động xuống dòng nếu ký tự kế tiếp bị vượt biên ngang màn hình
            x = 0;
            y += 16;
        }
        if (y + 16 > ST7735_HEIGHT) break; // Ngắt việc vẽ nếu tràn biên dọc màn hình
        
        st7735_draw_char(dev, x, y, *str, color, bg_color);
        x += 8; // Tịnh tiến điểm vẽ sang phải 8 pixel cho ký tự tiếp theo
        str++;
    }
}