#include "ppg_algorithm.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"

// [FIX 1] Thêm field 'initialized' để biết khi nào đã snap filter lần đầu.
// Trước khi snap, filter.w = 0 → AC ảo hàng chục nghìn LSB → window 1 hoàn toàn sai.
typedef struct {
    float w;           // Giá trị DC ước lượng hiện tại của bộ lọc IIR
    bool  initialized; // true = đã snap về DC thực lần đầu, false = chưa
} iir_filter_t;

// ============================================================
// Bộ đệm nội bộ (static — chỉ dùng trong file này)
// ============================================================
static uint32_t buffer_red[WINDOW_SIZE];   // Mẫu thô LED đỏ
static uint32_t buffer_ir[WINDOW_SIZE];    // Mẫu thô LED hồng ngoại
static float    ac_buffer_ir[WINDOW_SIZE]; // Thành phần AC của IR sau khi loại DC
static float    ac_buffer_red[WINDOW_SIZE];// Thành phần AC của RED sau khi loại DC
static int      sample_count         = 0;
static int      stabilization_counter= 0;
static iir_filter_t ir_filter, red_filter;

// ============================================================
// Bộ lọc high-pass IIR bậc 1 — loại bỏ thành phần DC
// alpha = 0.02  →  hằng số thời gian ≈ 0.5 giây @ 100 SPS
// Tần số cắt ≈ 0.32 Hz (giữ lại toàn bộ tín hiệu tim 0.8–2.5 Hz)
// ============================================================
static float apply_dc_removal(uint32_t sample, iir_filter_t *filter)
{
    filter->w = ((float)sample * 0.02f) + (filter->w * 0.98f);
    return (float)sample - filter->w;
}

// ============================================================
// PUBLIC: Khởi tạo toàn bộ trạng thái
// ============================================================
void ppg_algorithm_init(void)
{
    memset(buffer_red,    0, sizeof(buffer_red));
    memset(buffer_ir,     0, sizeof(buffer_ir));
    memset(ac_buffer_ir,  0, sizeof(ac_buffer_ir));
    memset(ac_buffer_red, 0, sizeof(ac_buffer_red));

    // [FIX 1] Khởi tạo cờ snap — filter.w = 0 không có nghĩa gì cho đến khi snap
    ir_filter.w            = 0.0f;
    ir_filter.initialized  = false;
    red_filter.w           = 0.0f;
    red_filter.initialized = false;

    sample_count          = 0;
    stabilization_counter = 0;
}

// ============================================================
// PUBLIC: Xử lý 1 mẫu mới
// Trả về true khi cửa sổ đầy và kết quả được ghi vào *result
// ============================================================
bool ppg_algorithm_process_sample(uint32_t red_sample, uint32_t ir_sample, ppg_result_t *result)
{
    if (result == NULL) return false;
    result->valid = false;

    // ----------------------------------------------------------
    // GATE 1: Nhấc tay — tín hiệu quá yếu, không có ngón tay
    // ----------------------------------------------------------
    if (red_sample < 30000 || ir_sample < 30000) {
        sample_count          = 0;
        stabilization_counter = 0;
        // [FIX 1] Reset snap để lần đặt ngón tay sau được khởi tạo lại đúng
        ir_filter.initialized  = false;
        red_filter.initialized = false;
        return false;
    }

    // ----------------------------------------------------------
    // GATE 2: Ngưỡng tín hiệu đặt ngón tay chưa đủ chắc
    // ----------------------------------------------------------
    if (ir_sample < 115000 || red_sample < 95000) {
        sample_count = 0;
        // Snap về giá trị hiện tại để không bị sốc biên độ khi tín hiệu tăng
        ir_filter.w  = (float)ir_sample;
        red_filter.w = (float)red_sample;
        stabilization_counter  = 0;
        // [FIX 1] Reset snap để bộ lọc bắt đầu lại từ DC đúng khi vượt ngưỡng
        ir_filter.initialized  = false;
        red_filter.initialized = false;
        return false;
    }

    // ----------------------------------------------------------
    // [FIX 1] Snap IIR filter lần đầu tiên tín hiệu đủ mạnh
    // Thay vì để filter.w leo từ 0 lên ~131k qua hàng trăm mẫu,
    // ta đặt ngay về DC thực → sai số còn <0.3% sau 200 mẫu warm-up
    // ----------------------------------------------------------
    if (!ir_filter.initialized) {
        ir_filter.w            = (float)ir_sample;
        ir_filter.initialized  = true;
        red_filter.w           = (float)red_sample;
        red_filter.initialized = true;
        stabilization_counter  = 0; // Bắt đầu đếm warm-up từ mức DC đúng
    }

    // ----------------------------------------------------------
    // GIAI ĐOẠN ỔN ĐỊNH (200 mẫu = 2 giây)
    // Cho bộ lọc bám đuôi tín hiệu DC, không lưu vào buffer
    // ----------------------------------------------------------
    if (stabilization_counter < STABILIZATION_SAMPLES) {
        stabilization_counter++;
        apply_dc_removal(ir_sample,  &ir_filter);
        apply_dc_removal(red_sample, &red_filter);
        // Log mỗi 1 giây (100 mẫu)
        if (stabilization_counter % 100 == 0) {
            ESP_LOGW("PPG_ALGO", "Đang ổn định bộ lọc... %.1f/2.0 giây",
                     stabilization_counter / 100.0f);
        }
        return false;
    }

    // ----------------------------------------------------------
    // THU THẬP MẪU VÀO BUFFER
    // ----------------------------------------------------------
    buffer_ir[sample_count]     = ir_sample;
    buffer_red[sample_count]    = red_sample;
    ac_buffer_ir[sample_count]  = apply_dc_removal(ir_sample,  &ir_filter);
    ac_buffer_red[sample_count] = apply_dc_removal(red_sample, &red_filter);
    sample_count++;

    if (sample_count < WINDOW_SIZE) {
        return false;
    }

    // ==========================================================
    // CỬA SỔ ĐẦY (200 mẫu) — TÍNH TOÁN HR VÀ SPO2
    // ==========================================================

    const int start_index = 5; // Bỏ qua 5 mẫu đầu để tránh vùng filter còn dao động

    // ----------------------------------------------------------
    // [FIX 3] DC = filter.w (IIR) thay vì mean số học của raw buffer
    // Lý do: AC cũng dùng filter.w làm tham chiếu DC (ac = sample - filter.w).
    // Nếu dùng hai loại DC khác nhau, tỉ số R sẽ bị sai khi có DC drift.
    // ----------------------------------------------------------
    float ir_dc  = ir_filter.w;
    float red_dc = red_filter.w;

    // Tìm max / min của thành phần AC trong cửa sổ
    float ac_ir_max  = ac_buffer_ir[start_index],  ac_ir_min  = ac_buffer_ir[start_index];
    float ac_red_max = ac_buffer_red[start_index], ac_red_min = ac_buffer_red[start_index];

    for (int i = start_index; i < WINDOW_SIZE; i++) {
        if (ac_buffer_ir[i]  > ac_ir_max)  ac_ir_max  = ac_buffer_ir[i];
        if (ac_buffer_ir[i]  < ac_ir_min)  ac_ir_min  = ac_buffer_ir[i];
        if (ac_buffer_red[i] > ac_red_max) ac_red_max = ac_buffer_red[i];
        if (ac_buffer_red[i] < ac_red_min) ac_red_min = ac_buffer_red[i];
    }

    float red_ac = ac_red_max - ac_red_min; // Biên độ AC của RED
    float ir_ac  = ac_ir_max  - ac_ir_min;  // Biên độ AC của IR

    // ----------------------------------------------------------
    // [FIX 4] Cảnh báo DC drift quá nhanh trong một cửa sổ
    // Drift > 2% trong 200 mẫu = ngón tay đang trượt hoặc áp lực thay đổi
    // ----------------------------------------------------------
    if (ir_dc > 0.0f) {
        float dc_drift_pct = fabsf((float)buffer_ir[WINDOW_SIZE - 1]
                                 - (float)buffer_ir[start_index]) / ir_dc * 100.0f;
        if (dc_drift_pct > 2.0f) {
            ESP_LOGW("PPG_ALGO", "DC drift %.1f%% — giữ ngón tay thật yên!", dc_drift_pct);
        }
    }

    // ----------------------------------------------------------
    // [FIX 2] CỔNG PERFUSION INDEX — loại cửa sổ nhiễu
    // PI < 0.5%: AC quá nhỏ so với DC, dynamic_threshold sẽ nằm trong vùng
    // nhiễu điện → thuật toán dò đỉnh sẽ đếm đỉnh giả → BPM 130-160 sai
    // ----------------------------------------------------------
    float pi_ir  = (ir_dc  > 0.0f) ? (ir_ac  / ir_dc)  : 0.0f;
    float pi_red = (red_dc > 0.0f) ? (red_ac / red_dc) : 0.0f;

    if (pi_ir < 0.005f || pi_red < 0.005f) {
        ESP_LOGW("PPG_ALGO",
                 "PI thấp: IR=%.2f%% RED=%.2f%% — nhấn ngón tay chặt hơn!",
                 pi_ir * 100.0f, pi_red * 100.0f);
        result->valid = false;
        goto do_window_shift; // Bỏ qua tính toán, vẫn dịch cửa sổ
    }

    // ----------------------------------------------------------
    // TÍNH SPO2
    // R = (AC_RED / DC_RED) / (AC_IR / DC_IR)
    // SpO2 = -45.06·R² + 30.35·R + 94.85  (đường chuẩn kinh nghiệm)
    // ----------------------------------------------------------
    {
        float current_spo2 = 0.0f;

        if (ir_ac > 0.0f) {
            float r = (red_ac / red_dc) / (ir_ac / ir_dc);
            current_spo2 = (-45.060f * r * r) + (30.354f * r) + 94.845f;
            if (current_spo2 > 100.0f) current_spo2 = 100.0f;
            if (current_spo2 <   0.0f) current_spo2 =   0.0f;
            ESP_LOGI("R_DEBUG",
                     "R=%.3f | RED_AC=%.1f RED_DC=%.1f | IR_AC=%.1f IR_DC=%.1f",
                     r, red_ac, red_dc, ir_ac, ir_dc);
        }

        // ----------------------------------------------------------
        // DÒ ĐỈNH NHỊP TIM
        // Ngưỡng động 70%: loại bỏ đỉnh phụ dicrotic notch
        // Dead-time 45 mẫu (450 ms): loại nhiễu răng cưa
        // ----------------------------------------------------------
        int peak_indices[50];
        int peak_count = 0;
        float dynamic_threshold = ac_ir_min + (ac_ir_max - ac_ir_min) * 0.7f;

        for (int i = start_index + 1; i < WINDOW_SIZE - 1; i++) {
            if (ac_buffer_ir[i] > ac_buffer_ir[i - 1] &&
                ac_buffer_ir[i] > ac_buffer_ir[i + 1] &&
                ac_buffer_ir[i] > dynamic_threshold) {
                peak_indices[peak_count++] = i;
                i += 45;
                if (peak_count >= 50) break;
            }
        }

        double current_bpm = 0.0;
        if (peak_count > 1) {
            int total_intervals = 0;
            int valid_intervals = 0;
            for (int i = 1; i < peak_count; i++) {
                int interval = peak_indices[i] - peak_indices[i - 1];
                // Khoảng cách hợp lệ: 35–160 mẫu ≈ 37.5–171 BPM
                if (interval >= 35 && interval <= 160) {
                    total_intervals += interval;
                    valid_intervals++;
                }
            }
            if (valid_intervals > 0) {
                double avg_interval = (double)total_intervals / valid_intervals;
                current_bpm = (60.0 * SAMPLE_RATE) / avg_interval;
            }
        }

        ESP_LOGI("PPG_DEBUG",
                 "BPM = %.1f | SpO2 = %.1f%% | Đỉnh = %d | PI_IR = %.2f%%",
                 current_bpm, current_spo2, peak_count, pi_ir * 100.0f);

        // Validation sinh học cuối cùng
        if (current_spo2 >= 80.0f && current_spo2 <= 100.0f &&
            current_bpm  >= 50.0  && current_bpm  <= 140.0) {
            result->heart_rate = (float)current_bpm;
            result->spo2       = current_spo2;
            result->valid      = true;
        }
        // Nếu không hợp lệ, result->valid đã = false từ đầu hàm
    }

    // ==========================================================
    // DỊCH CHUYỂN CUỐN CHIẾU (sliding window với 50% overlap)
    // Giữ lại OVERLAP_SIZE = 100 mẫu cuối làm đầu cửa sổ tiếp theo
    // ==========================================================
do_window_shift: ;
    {
        const int shift_size = WINDOW_SIZE - OVERLAP_SIZE; // = 100 mẫu
        memmove(&buffer_red[0],    &buffer_red[shift_size],    OVERLAP_SIZE * sizeof(uint32_t));
        memmove(&buffer_ir[0],     &buffer_ir[shift_size],     OVERLAP_SIZE * sizeof(uint32_t));
        memmove(&ac_buffer_ir[0],  &ac_buffer_ir[shift_size],  OVERLAP_SIZE * sizeof(float));
        memmove(&ac_buffer_red[0], &ac_buffer_red[shift_size], OVERLAP_SIZE * sizeof(float));
        sample_count = OVERLAP_SIZE;
    }
    return true;
}