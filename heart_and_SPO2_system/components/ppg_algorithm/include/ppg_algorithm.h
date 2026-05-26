#ifndef PPG_ALGORITHM_H
#define PPG_ALGORITHM_H

#include <stdint.h>
#include <stdbool.h>

#define SAMPLE_RATE 100 // Tốc độ lấy mẫu mặc định (có thể điều chỉnh theo cấu hình của MAX30102)
#define WINDOW_SIZE 200 // Kích thước bộ đệm mẫu để xử lý thuật toán
#define OVERLAP_SIZE 100 // Số mẫu chồng lắp giữa các cửa sổ xử lý (giúp mượt mà hơn)

// cấu trúc kết quả đầu ra của thuật toán PPG
typedef struct{
    float heart_rate; 
    float spo2;
    bool valid; // cờ hiệu để đánh dấu kết quả có hợp lệ hay không (ví dụ: tín hiệu quá yếu, nhiều nhiễu, không đủ mẫu...)
} ppg_result_t;

// hàm khởi tạo bộ lọc và các tham số cần thiết cho thuật toán xử lý tín hiệu PPG
void ppg_algorithm_init(void);

// hàm xử lý mẫu PPG mới, trả về true nếu có kết quả mới sau mỗi 400 mẫu (chu kỳ tính toán sẽ là 4 giây nếu sample_rate là 100 SPs, chu kỳ gối đầu là 2 giây)
bool ppg_algorithm_process_sample(uint32_t red_sample, uint32_t ir_sample, ppg_result_t *result);

#endif // PPG_ALGORITHM_H