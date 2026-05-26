#ifndef PPG_ALGORITHM_H
#define PPG_ALGORITHM_H

#include <stdint.h>
#include <stdbool.h>

#define SAMPLE_RATE          100  // Tốc độ lấy mẫu (SPS), khớp với cấu hình MAX30102
#define WINDOW_SIZE          200  // Kích thước cửa sổ xử lý (200 mẫu = 2 giây @ 100 SPS)
#define OVERLAP_SIZE         100  // Số mẫu gối đầu giữa hai cửa sổ liên tiếp (50% overlap)
#define STABILIZATION_SAMPLES 200 // [FIX 1] Tăng từ 30 → 200: đủ thời gian để IIR filter hội tụ
                                  // từ giá trị snap về DC thực (~131k), sai số còn <0.3% sau 200 mẫu

// Cấu trúc kết quả đầu ra của thuật toán PPG
typedef struct {
    float heart_rate; // Nhịp tim (BPM)
    float spo2;       // Độ bão hòa oxy (%)
    bool  valid;      // true = kết quả tin cậy, false = tín hiệu yếu / nhiễu / PI thấp
} ppg_result_t;

// Khởi tạo bộ đệm, bộ lọc IIR và tất cả trạng thái nội bộ
void ppg_algorithm_init(void);

// Đưa 1 mẫu mới vào pipeline. Trả về true khi cửa sổ 200 mẫu đã đầy và
// kết quả mới được ghi vào *result (chu kỳ cập nhật: mỗi 1 giây nhờ 50% overlap)
bool ppg_algorithm_process_sample(uint32_t red_sample, uint32_t ir_sample, ppg_result_t *result);

#endif // PPG_ALGORITHM_H