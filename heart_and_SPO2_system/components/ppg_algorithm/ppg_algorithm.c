#include "ppg_algorithm.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"

typedef struct {
    float w;
} iir_filter_t; 

static uint32_t buffer_red[WINDOW_SIZE];  // Lưu trữ mẫu thô của LED đỏ
static uint32_t buffer_ir[WINDOW_SIZE];  // Lưu trữ mẫu thô của LED hồng ngoại
static float ac_buffer_ir[WINDOW_SIZE];  // Lưu trữ thành phần AC của tín hiệu IR
static float ac_buffer_red[WINDOW_SIZE]; // Lưu trữ thành phần AC của tín hiệu RED
static int sample_count = 0;  // Đếm số mẫu đã thu thập trong cửa sổ hiện tại
static iir_filter_t ir_filter, red_filter; // Bộ lọc IIR đơn giản để loại bỏ thành phần DC
static int stabilization_counter = 0;
#define STABILIZATION_SAMPLES 30 // Số mẫu cần để bộ lọc ổn định, khoảng 0.3 giây nếu sample_rate là 100 SPs

static float apply_dc_removal(uint32_t sample, iir_filter_t *filter) {// Bộ lọc IIR đơn giản để loại bỏ thành phần DC, alpha = 0.02
    // Cập nhật giá trị trung bình động của thành phần DC. apha = 0.02 có nghĩa là giá trị DC sẽ chiếm 2% ảnh hưởng của mẫu mới và 98% ảnh hưởng của giá trị DC trước đó, giúp theo dõi chậm rãi và ổn định hơn
    filter->w = ((float)sample * 0.02f) + (filter->w * 0.98f);
    // Trả về giá trị đã được loại bỏ thành phần DC, chỉ còn lại AC = giá trị mẫu trừ đi giá trị trung bình động của thành phần DC
    return (float)sample - filter->w;
}

void ppg_algorithm_init(void) { 
    memset(buffer_red, 0, sizeof(buffer_red)); // Khởi tạo bộ đệm mẫu về 0
    memset(buffer_ir, 0, sizeof(buffer_ir)); 
    memset(ac_buffer_ir, 0, sizeof(ac_buffer_ir));
    memset(ac_buffer_red, 0, sizeof(ac_buffer_red));
    ir_filter.w = 0.0f; // Khởi tạo giá trị trung bình động của bộ lọc IIR về 0
    red_filter.w = 0.0f;
    sample_count = 0;
}

bool ppg_algorithm_process_sample(uint32_t red_sample, uint32_t ir_sample, ppg_result_t *result) {
    if (result == NULL) return false;
    result->valid = false; 

    // 1. Bộ lọc nhấc tay cơ bản
    if (red_sample < 30000 || ir_sample < 30000) { 
        sample_count = 0; 
        stabilization_counter = 0; // Đặt lại bộ đếm ổn định khi tín hiệu quá yếu, giúp đảm bảo rằng thuật toán chỉ bắt đầu xử lý khi tín hiệu đã ổn định sau khi ngón tay được đặt lại
        return false; 
    }

    // 2. Ngưỡng cổng đóng băng giữ ổn định ngón tay
    if (ir_sample < 115000 || red_sample < 95000) {
        sample_count = 0; 
        ir_filter.w = (float)ir_sample; // Đặt lại giá trị trung bình động của bộ lọc IIR về mẫu hiện tại để tránh sốc khi ngón tay được đặt lại
        red_filter.w = (float)red_sample; // Đặt lại giá trị trung bình động của bộ lọc IIR về mẫu hiện tại để tránh sốc khi ngón tay được đặt lại
        stabilization_counter = 0; // Đặt lại bộ đếm ổn định để bắt đầu đếm lại khi tín hiệu trở lại mức đủ mạnh, giúp đảm bảo rằng thuật toán chỉ bắt đầu xử lý khi tín hiệu đã ổn định sau khi ngón tay được đặt lại
        return false; 

    }

    if (stabilization_counter < STABILIZATION_SAMPLES) {
        stabilization_counter++;
        
        // Cho bộ lọc IIR "bám đuôi" tín hiệu DC trong thời gian chờ để tránh sốc biên độ
        apply_dc_removal(ir_sample, &ir_filter);
        apply_dc_removal(red_sample, &red_filter);
        
        // Cứ mỗi 1 giây trong giai đoạn chờ thì in log thông báo cho người dùng
        if (stabilization_counter % 10 == 0) {
            ESP_LOGW("PPG_ALGO", "Đang đợi tín hiệu ổn định... %d/0.3 giây", stabilization_counter / 10);
        }
        return false; // Chưa đo đạc gì cả, thoát sớm!
    }

    buffer_ir[sample_count] = ir_sample; // Lưu mẫu thô vào bộ đệm
    buffer_red[sample_count] = red_sample; // Lưu mẫu thô vào bộ đệm
    ac_buffer_ir[sample_count] = apply_dc_removal(ir_sample, &ir_filter); // Tính thành phần AC và lưu vào bộ đệm, xóa thành phần DC để chỉ còn lại dao động xung quanh 0
    ac_buffer_red[sample_count] = apply_dc_removal(red_sample, &red_filter); 
    sample_count++; 

    if (sample_count >= WINDOW_SIZE) { 



        int start_index = 5; // Bỏ qua 5 mẫu đầu ổn định bộ lọc
        
        uint64_t red_sum = 0;// lưu tổng giá trị thô red vào đây  
        uint64_t ir_sum = 0; // lưu tổng giá trị thô ir vào đây
        for (int i = start_index; i < WINDOW_SIZE; i++) { 
            red_sum += buffer_red[i];
            ir_sum += buffer_ir[i];
        }
        float red_dc = (float)red_sum / (WINDOW_SIZE - start_index); // dc = giá trị trung bình toàn bộ mẫu thô trong cửa sổ hiện tại 0
        float ir_dc = (float)ir_sum / (WINDOW_SIZE - start_index); // dc = giá trị trung bình toàn bộ mẫu thô trong cửa sổ hiện tại 0

        float ac_ir_max = ac_buffer_ir[start_index], ac_ir_min = ac_buffer_ir[start_index]; // đặt giá trị cực đại và cực tiểu ban đầu của thành phần AC bằng giá trị tại start_index để bắt đầu tìm kiếm trong cửa sổ
        float ac_red_max = ac_buffer_red[start_index], ac_red_min = ac_buffer_red[start_index];
        
        for (int i = start_index; i < WINDOW_SIZE; i++) {
            if (ac_buffer_ir[i] > ac_ir_max)   ac_ir_max = ac_buffer_ir[i];
            if (ac_buffer_ir[i] < ac_ir_min)   ac_ir_min = ac_buffer_ir[i];
            if (ac_buffer_red[i] > ac_red_max) ac_red_max = ac_buffer_red[i];
            if (ac_buffer_red[i] < ac_red_min) ac_red_min = ac_buffer_red[i];
        } // lọc qua 1 lần để tìm giá trị cực đại và cực tiểu của thành phần AC trong cửa sổ mẫu hiện tại, giúp xác định biên độ dao động của tín hiệu PPG sau khi đã loại bỏ thành phần DC.

        float red_ac = ac_red_max - ac_red_min; // Biên độ AC của tín hiệu RED là sự khác biệt giữa giá trị cực đại và cực tiểu của thành phần AC trong cửa sổ mẫu hiện tại, phản ánh cường độ dao động của tín hiệu PPG sau khi đã loại bỏ thành phần DC, giúp đánh giá chất lượng tín hiệu và tính toán SpO2 chính xác hơn
        float ir_ac = ac_ir_max - ac_ir_min; // Biên độ AC của tín hiệu IR là sự khác biệt giữa giá trị cực đại và cực tiểu của thành phần AC trong cửa sổ mẫu hiện tại, phản ánh cường độ dao động của tín hiệu PPG sau khi đã loại bỏ thành phần DC, giúp đánh giá chất lượng tín hiệu và tính toán SpO2 chính xác hơn

        float current_spo2 = 0.0f; // Tính toán SpO2 dựa trên tỉ số R = (AC_RED/DC_RED) / (AC_IR/DC_IR), sau đó áp dụng công thức chuyển đổi từ R sang SpO2
        if (red_dc > 0 && ir_dc > 0 && ir_ac > 0) {  // Chỉ tính toán SpO2 nếu thành phần DC dương và có biên độ AC hợp lệ để tránh chia cho 0 hoặc kết quả không hợp lý
            float r = (red_ac / red_dc) / (ir_ac / ir_dc);
            current_spo2 = (-45.060f * r * r) + (30.354f * r) + 94.845f;
            if (current_spo2 > 100.0f) current_spo2 = 100.0f;
            if (current_spo2 < 0.0f)   current_spo2 = 0.0f;
            ESP_LOGI("R_DEBUG","R=%.3f | RED_AC=%.1f RED_DC=%.1f | IR_AC=%.1f IR_DC=%.1f",r,red_ac,red_dc,ir_ac,ir_dc);
        }

        // --- Thuật toán dò đỉnh nhịp tim (Chống Dicrotic Notch) ---
        int peak_indices[50]; // Lưu chỉ số của các đỉnh được phát hiện, giới hạn tối đa 50 đỉnh trong cửa sổ
        int peak_count = 0; // Đếm số lượng đỉnh được phát hiện
        
        // Ngưỡng động 55% chặn hoàn toàn đỉnh phụ dicrotic notch thấp
        float dynamic_threshold = ac_ir_min + (ac_ir_max - ac_ir_min) * 0.5f; 

        for (int i = start_index + 1; i < WINDOW_SIZE - 1; i++) { // Bắt đầu từ start_index để tránh vùng ổn định bộ lọc
            if (ac_buffer_ir[i] > ac_buffer_ir[i - 1] && ac_buffer_ir[i] > ac_buffer_ir[i + 1]) {
                if (ac_buffer_ir[i] > dynamic_threshold) { // Chỉ chấp nhận đỉnh nếu vượt ngưỡng động
                    peak_indices[peak_count++] = i; 
                    i += 22; // Dead-time 220ms loại bỏ răng cưa nhiễu dính liền
                    if (peak_count >= 50) break;
                }
            }
        }

        double current_bpm = 0.0;
        if (peak_count > 1) {
            int total_intervals = 0;// Tổng khoảng thời gian giữa các đỉnh được phát hiện, sẽ được sử dụng để tính toán BPM trung bình trong cửa sổ mẫu hiện tại
            int valid_intervals = 0;// Đếm số lượng khoảng thời gian hợp lệ giữa các đỉnh, chỉ tính những khoảng thời gian nằm trong dải sinh học từ 50 đến 150 BPM để loại bỏ các đỉnh nhiễu không hợp lý
            
            for (int i = 1; i < peak_count; i++) {// Tính khoảng thời gian giữa các đỉnh được phát hiện, chỉ tính những khoảng thời gian nằm trong dải sinh học từ 50 đến 150 BPM để loại bỏ các đỉnh nhiễu không hợp lý
                int interval = peak_indices[i] - peak_indices[i - 1]; 
                if (interval >= 35 && interval <= 160) { // Giới hạn dải sinh học từ 50 đến 150 BPM
                    total_intervals += interval;
                    valid_intervals++;
                }
            }
            
            if (valid_intervals > 0) {
                double avg_interval = (double)total_intervals / valid_intervals;
                current_bpm = (60.0 * SAMPLE_RATE) / avg_interval;
            }
        }

        ESP_LOGI("PPG_DEBUG", "Thực tế: BPM = %.1f, SpO2 = %.1f%% | Đếm được %d đỉnh", 
                 current_bpm, current_spo2, peak_count);

        if (current_spo2 >= 80.0f && current_spo2 <= 100.0f && current_bpm >= 50.0 && current_bpm <= 140.0) {
            result->heart_rate = (float)current_bpm; // Gán giá trị float chuẩn
            result->spo2 = current_spo2;
            result->valid = true;
        } else {
            result->valid = false; 
        }

        // TÍNH TOÁN DỊCH CHUYỂN CUỐN CHIẾU CHUẨN XÁC
        int shift_size = WINDOW_SIZE - OVERLAP_SIZE; // 100 - 50 = 50 mẫu

        // Đưa dữ liệu vùng chồng lắp (Overlap) từ đuôi mảng về đầu mảng
        memmove(&buffer_red[0], &buffer_red[shift_size], OVERLAP_SIZE * sizeof(uint32_t));// Dịch chuyển 50 mẫu cuối của buffer_red về đầu mảng để chuẩn bị cho cửa sổ xử lý tiếp theo
        memmove(&buffer_ir[0], &buffer_ir[shift_size], OVERLAP_SIZE * sizeof(uint32_t));
        memmove(&ac_buffer_ir[0], &ac_buffer_ir[shift_size], OVERLAP_SIZE * sizeof(float));
        memmove(&ac_buffer_red[0], &ac_buffer_red[shift_size], OVERLAP_SIZE * sizeof(float));

        // Đặt bộ đếm bằng chính lượng mẫu gối đầu đã giữ lại để thu thập tiếp vùng trống
        sample_count = OVERLAP_SIZE; 

        return true; 
    }
    return false; 
}