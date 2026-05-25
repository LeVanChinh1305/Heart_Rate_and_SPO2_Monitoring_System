#pragma once 

#include <stdint.h> // thư viện chuẩn cho kiểu dữ liệu nguyên thủy(uint8_t, uint32_t...)
#include <stdbool.h>            // Thư viện cho kiểu dữ liệu logic (true, false)
#include "driver/i2c_master.h" // thư viện cho giao tiếp I2C trên ESP32
#include "esp_err.h" // thư viện cho xử lý lỗi trên ESP32(esp_ok, esp_fail...)

#ifdef __cplusplus // Nếu đang biên dịch với C++, sử dụng extern "C" để đảm bảo tên hàm không bị thay đổi
extern "C" {
#endif

// thông tin về thiết bị MAX30102
#define MAX30102_I2C_ADDR 0x57 // Địa chỉ I2C 7-bit mặc định của cảm biến
#define MAX30102_PART_ID_EXPECTED 0x15 // giá trị ID phần cứng của MAX30102

// register map của MAX30102 - Dựa theo Datasheet
// Ngắt và Trạng thái 
#define MAX30102_REG_INTR_STATUS_1  0x00    // Trạng thái ngắt 1: FIFO đầy, Dữ liệu sẵn sàng, v.v.
                                                // bit 7: FIFO Almost Full: Khi số mẫu trong FIFO đạt đến ngưỡng đã cấu hình (ví dụ: 0x0F), bit này sẽ được set, báo hiệu rằng cần đọc dữ liệu để tránh mất mẫu do FIFO đầy. Việc sử dụng ngắt này giúp tối ưu hóa việc đọc dữ liệu, chỉ khi nào có đủ mẫu mới hoặc gần đầy thì mới thực hiện đọc, thay vì phải liên tục kiểm tra. cấu hình trong thanh ghi FIFO_CONFIG (0x08) với trường FIFO_A_FULL[3:0] để thiết lập ngưỡng này.
                                                // bit 6: New Data Ready : Khi một mẫu mới đã được ghi vào FIFO, bit này sẽ được set. Việc sử dụng ngắt này giúp đảm bảo rằng vi điều khiển chỉ đọc dữ liệu khi có mẫu mới, tránh việc đọc dữ liệu cũ hoặc không có dữ liệu nào trong FIFO.
                                                // bit 5: Ambient Light Cancellation Overflow: Khi cảm biến phát hiện ánh sáng môi trường quá mạnh, có thể gây ra hiện tượng quá tải trong quá trình đo, bit này sẽ được set để báo hiệu rằng kết quả đo có thể không chính xác do ánh sáng môi trường ảnh hưởng. Việc kiểm tra bit này giúp đảm bảo rằng dữ liệu thu được là đáng tin cậy và có thể được sử dụng cho các ứng dụng y tế hoặc theo dõi sức khỏe.
                                                // bit 4: Proximity Interrupt: Khi cảm biến phát hiện vật thể gần (dựa trên ngưỡng đã cấu hình), bit này sẽ được set. Việc sử dụng ngắt này giúp tối ưu hóa việc phát hiện sự hiện diện của vật thể gần, có thể được sử dụng trong các ứng dụng như phát hiện khi người dùng đặt ngón tay lên cảm biến để bắt đầu đo nhịp tim hoặc SpO2. Nếu bit này không được set khi có vật thể gần, có thể có vấn đề với cảm biến hoặc cấu hình ngưỡng không phù hợp.
                                                // bit 0: Power Ready: Khi cảm biến đã sẵn sàng hoạt động sau khi bật nguồn hoặc sau khi reset, bit này sẽ được set. Việc kiểm tra bit này giúp đảm bảo rằng cảm biến đã khởi động hoàn tất và có thể nhận lệnh cấu hình hoặc đọc dữ liệu mà không gặp lỗi do thiết bị chưa sẵn sàng. Nếu bit này không được set sau khi khởi động, có thể có vấn đề về nguồn điện hoặc phần cứng của cảm biến.
#define MAX30102_REG_INTR_STATUS_2  0x01    // Trạng thái ngắt 2: Trạng thái nhiệt độ, chỉ dùng bit 1 để kiểm tra xem dữ liệu nhiệt độ đã sẵn sàng hay chưa. Khi bit này được set, có nghĩa là cảm biến đã hoàn thành quá trình đo nhiệt độ và dữ liệu nhiệt độ có thể được đọc từ các thanh ghi nhiệt độ (0x1F và 0x20). Việc kiểm tra bit này trước khi đọc dữ liệu nhiệt độ giúp đảm bảo rằng bạn đang nhận được dữ liệu mới nhất và chính xác, tránh việc đọc dữ liệu cũ hoặc không hợp lệ.
#define MAX30102_REG_INTR_ENABLE_1  0x02    // Cho phép/Cấm các loại ngắt 1 
#define MAX30102_REG_INTR_ENABLE_2  0x03    // Cho phép/Cấm các loại ngắt 2(trạng thái ngắt nhiệt độ được quản lý riêng)
// Quản lý bộ đệm FIFO 
#define MAX30102_REG_FIFO_WR_PTR    0x04    // Con trỏ ghi: Vị trí cảm biến sẽ ghi dữ liệu tiếp theo : FIFO_WR_PTR[4:0] là một con trỏ 5-bit, có thể có giá trị từ 0 đến 31, tương ứng với 32 vị trí trong FIFO. Khi cảm biến ghi một mẫu mới vào FIFO, nó sẽ sử dụng con trỏ này để xác định vị trí lưu trữ mẫu tiếp theo. Sau khi ghi xong, con trỏ sẽ tự động tăng lên 1 (theo kiểu vòng tròn), nếu đạt đến giá trị 31 và có thêm mẫu mới, nó sẽ quay lại vị trí 0. Việc quản lý con trỏ ghi này giúp đảm bảo rằng dữ liệu được lưu trữ liên tục trong FIFO mà không bị mất mát, miễn là bạn đọc dữ liệu kịp thời trước khi FIFO đầy.
#define MAX30102_REG_OVF_COUNTER    0x05    // Bộ đếm tràn: Số mẫu đã bị mất do FIFO đầy: OVF_COUNTER[4:0] là một bộ đếm 5-bit, có thể có giá trị từ 0 đến 31, được sử dụng để theo dõi số lượng mẫu đã bị mất do FIFO đầy. Mỗi khi cảm biến cố gắng ghi một mẫu mới vào FIFO nhưng phát hiện rằng FIFO đã đầy (tức là con trỏ ghi đã đạt đến vị trí cuối cùng và không có chỗ trống nào để lưu mẫu mới), nó sẽ tăng giá trị của OVF_COUNTER lên 1. Việc theo dõi OVF_COUNTER giúp bạn đánh giá hiệu suất của hệ thống đọc dữ liệu: nếu giá trị này tăng lên thường xuyên, có nghĩa là bạn đang mất nhiều mẫu do không đọc dữ liệu kịp thời, và có thể cần phải tối ưu hóa việc đọc dữ liệu hoặc giảm tốc độ lấy mẫu để tránh mất mát dữ liệu quan trọng.
#define MAX30102_REG_FIFO_RD_PTR    0x06    // Con trỏ đọc: Vị trí vi điều khiển sẽ đọc dữ liệu tiếp theo: FIFO_RD_PTR[4:0] là một con trỏ 5-bit, có thể có giá trị từ 0 đến 31, tương ứng với 32 vị trí trong FIFO. Khi vi điều khiển đọc một mẫu dữ liệu từ FIFO, nó sẽ sử dụng con trỏ này để xác định vị trí của mẫu tiếp theo cần đọc. Sau khi đọc xong, con trỏ sẽ tự động tăng lên 1 (theo kiểu vòng tròn), nếu đạt đến giá trị 31 và có thêm mẫu mới, nó sẽ quay lại vị trí 0. Việc quản lý con trỏ đọc này giúp đảm bảo rằng bạn đang đọc dữ liệu một cách tuần tự và không bỏ sót mẫu nào trong FIFO, miễn là bạn đọc dữ liệu kịp thời trước khi FIFO đầy.
#define MAX30102_REG_FIFO_DATA      0x07    // Cổng dữ liệu: Đọc dữ liệu thô (Red/IR) từ đây: FIFO_DATA[7:0] là cổng dữ liệu 8-bit được sử dụng để đọc dữ liệu thô từ FIFO của cảm biến. 
// configuration registers
#define MAX30102_REG_FIFO_CONFIG    0x08    // Cấu hình trung bình mẫu, cuộn FIFO, mức ngắt đầy
                                                // FIFO_A_FULL[3:0] ở bit 0, 1, 2, 3: Đây là một trường 4-bit trong thanh ghi FIFO_CONFIG, được sử dụng để thiết lập ngưỡng "gần đầy" cho FIFO. Khi số lượng mẫu trong FIFO đạt đến giá trị đã cấu hình trong FIFO_A_FULL, bit ngắt "FIFO Almost Full" sẽ được kích hoạt, báo hiệu rằng cần đọc dữ liệu để tránh mất mẫu do FIFO đầy. Việc cấu hình ngưỡng này giúp tối ưu hóa việc đọc dữ liệu, chỉ khi nào có đủ mẫu mới hoặc gần đầy thì mới thực hiện đọc, thay vì phải liên tục kiểm tra.
                                                // FIFO_ROLLOVER_EN[0] ở bit 4: Khi bit này được set, nếu FIFO đầy và có thêm mẫu mới, cảm biến sẽ tự động ghi đè lên mẫu cũ nhất trong FIFO (theo kiểu vòng tròn), thay vì ngừng ghi dữ liệu mới. Việc sử dụng tính năng này giúp đảm bảo rằng bạn luôn có dữ liệu mới nhất trong FIFO, nhưng cũng cần lưu ý rằng nếu không đọc dữ liệu kịp thời, bạn có thể mất các mẫu cũ mà chưa kịp xử lý.
                                                    // khi = 0 : FIFO sẽ ngừng ghi dữ liệu mới khi đầy, và bạn sẽ mất các mẫu mới nếu không đọc kịp thời.
                                                    // khi = 1 : FIFO sẽ tự động ghi đè lên mẫu cũ nhất
                                                // SMP_AVE[2:0] ở bit 7,6,5: Đây là một trường 3-bit trong thanh ghi FIFO_CONFIG, được sử dụng để thiết lập số lượng mẫu trung bình trước khi lưu vào FIFO. Khi bạn cấu hình SMP_AVE với một giá trị nhất định (ví dụ: 0x00 cho không trung bình, 0x01 cho trung bình 2 mẫu, 0x02 cho trung bình 4 mẫu, v.v.), cảm biến sẽ lấy số lượng mẫu liên tiếp tương ứng, cộng lại và chia cho số mẫu đó để tạo ra một giá trị trung bình duy nhất, sau đó mới lưu kết quả này vào FIFO. Việc sử dụng tính năng trung bình mẫu giúp giảm nhiễu và cải thiện chất lượng dữ liệu, đặc biệt hữu ích trong các ứng dụng y tế hoặc theo dõi sức khỏe.
#define MAX30102_REG_MODE_CONFIG    0x09    // Cấu hình chế độ: Shutdown(bit 7), Reset(bit 6), Mode[2:0] bit 0->2: HR, SpO2, Multi-LED 
                                                //bit 7 (SHDN): Khi bit này được set, cảm biến sẽ chuyển sang chế độ shutdown, tắt hầu hết các chức năng để tiết kiệm điện năng. Trong chế độ này, cảm biến sẽ tiêu thụ rất ít năng lượng, nhưng cũng sẽ không thực hiện bất kỳ phép đo nào cho đến khi bit này được xóa (clear). 
                                                    // khi = 1" chế độ tiết kiệm điện 
                                                    // khi= 0 : chế độ hoạt động bình thường 
                                                // bit 6 (RESET): Khi bit này được set, cảm biến sẽ thực hiện một phép reset phần cứng, khởi động lại toàn bộ thanh ghi về giá trị mặc định. Sau khi reset hoàn tất, bit này sẽ tự động xóa (clear). Việc sử dụng bit reset giúp đảm bảo rằng cảm biến bắt đầu hoạt động từ một trạng thái sạch sẽ, đặc biệt hữu ích khi gặp sự cố hoặc khi cần thiết lập lại cấu hình.
                                                // bit 2-0 (Mode[2:0]): Đây là một trường 3-bit trong thanh ghi MODE_CONFIG, được sử dụng để chọn chế độ hoạt động của cảm biến. Các giá trị có thể có bao gồm:
                                                    // 0x00: Chế độ Heart Rate (HR) - Sử dụng LED đỏ để đo nhịp tim.
                                                    // 0x01: Chế độ SpO2 - Sử dụng cả LED đỏ và hồng ngoại để đo nhịp tim và nồng độ oxy trong máu.
                                                    // 0x02: Chế độ Multi-LED - Cho phép sử dụng nhiều LED khác nhau để đo nhiều thông số cùng lúc, như nhịp tim, SpO2, và các chỉ số khác. Việc chọn chế độ phù hợp giúp tối ưu hóa hiệu suất và chất lượng dữ liệu thu được từ cảm biến, tùy thuộc vào ứng dụng cụ thể mà bạn đang phát triển.
#define MAX30102_REG_SPO2_CONFIG    0x0A    // Cấu hình dải ADC, tốc độ lấy mẫu, độ rộng xung LED
                                                // B7-6-5: SPO2_ADC_RGE[2:0]: Đây là một trường 3-bit trong thanh ghi SPO2_CONFIG, được sử dụng để thiết lập dải ADC tối đa cho phép khi đo SpO2. Các giá trị có thể có bao gồm:
                                                    // 0x00: Dải ADC tối đa 2038nA
                                                    // 0x01: Dải ADC tối đa 4096nA
                                                    // 0x02: Dải ADC tối đa 8192nA
                                                    // 0x03: Dải ADC tối đa 16384nA
                                                // B4-3-2: SPO2_SR[2:0]: Đây là một trường 3-bit trong thanh ghi SPO2_CONFIG, được sử dụng để thiết lập tốc độ lấy mẫu (Sample Rate) khi đo SpO2. Các giá trị có thể có bao gồm:
                                                    // 000: 50 samples/sec
                                                    // 001: 100 samples/sec
                                                    // 010: 200 samples/sec
                                                    // 011: 400 samples/sec
                                                    // 100: 800 samples/sec
                                                    // 101: 1000 samples/sec
                                                    // 110: 1600 samples/sec
                                                    // 111: 3200 samples/sec
                                                // B1-0: SPO2_PW[1:0]: Đây là một trường 2-bit trong thanh ghi SPO2_CONFIG, được sử dụng để thiết lập độ rộng xung LED và độ phân giải ADC khi đo SpO2. Các giá trị có thể có bao gồm:
                                                    // 00: Độ rộng xung LED 69us, độ phân giải ADC 15-bit
                                                    // 01: Độ rộng xung LED 118us, độ phân giải ADC
                                                    // 10: Độ rộng xung LED 215us, độ phân giải ADC 17-bit
                                                    // 11: Độ rộng xung LED 411us, độ phân giải ADC
#define MAX30102_REG_LED1_PA        0x0C    // Cường độ dòng điện cho LED 1 (Red) : bit7 đến bit 0 của thanh ghi này tương ứng với cường độ dòng điện từ 0mA đến 50mA, với mỗi bước tăng tương đương khoảng 0.2mA. Ví dụ, nếu bạn muốn đặt cường độ dòng điện cho LED đỏ là 10mA, bạn có thể tính giá trị cần ghi vào thanh ghi như sau: 10mA / 0.2mA per step = 50 steps, do đó bạn sẽ ghi giá trị 50 (0x32) vào thanh ghi LED1_PA để đạt được cường độ dòng điện mong muốn.
#define MAX30102_REG_LED2_PA        0x0D    // Cường độ dòng điện cho LED 2 (IR) : bit7 đến bit 0 của thanh ghi này tương ứng với cường độ dòng điện từ 0mA đến 50mA, với mỗi bước tăng tương đương khoảng 0.2mA. Ví dụ, nếu bạn muốn đặt cường độ dòng điện cho LED hồng ngoại là 10mA, bạn có thể tính giá trị cần ghi vào thanh ghi như sau: 10mA / 0.2mA per step = 50 steps, do đó bạn sẽ ghi giá trị 50 (0x32) vào thanh ghi LED2_PA để đạt được cường độ dòng điện mong muốn.
#define MAX30102_REG_PILOT_PA       0x10    // Dòng điện cho LED mồi (Proximity Mode): bit7 đến bit 0 của thanh ghi này tương ứng với cường độ dòng điện từ 0mA đến 50mA, với mỗi bước tăng tương đương khoảng 0.2mA. Ví dụ, nếu bạn muốn đặt cường độ dòng điện cho LED mồi là 10mA, bạn có thể tính giá trị cần ghi vào thanh ghi như sau: 10mA / 0.2mA per step = 50 steps, do đó bạn sẽ ghi giá trị 50 (0x32) vào thanh ghi PILOT_PA để đạt được cường độ dòng điện mong muốn.
#define MAX30102_REG_MULTILED_CTRL1 0x11    // Điều khiển Slot 1 & 2 trong chế độ Multi-LED
                                                // slot 1:[2:0] bit 2-1-0
                                                // slot 2:[2:0] bit 6-5-4
#define MAX30102_REG_MULTILED_CTRL2 0x12    // Điều khiển Slot 3 & 4 trong chế độ Multi-LED
                                                    // slot 3:[2:0] bit 2-1-0
                                                    // slot 4:[2:0] bit 6-5-4
                                            // bảng chọn led 
                                                // 000: Slot bị vô hiệu hóa (không sử dụng LED nào)
                                                // 001: Sử dụng LED1 (Red)
                                                // 010: Sử dụng LED2 (IR)
                                                // 011: none 
                                                // 100: none 
                                                // 101: Sử dụng PILOT_PA (Red) - LED mồi cho chế độ Proximity
                                                // 110: Sử dụng PILOT_PA (IR) - LED mồi cho chế độ Proximity

// Cảm biến nhiệt độ tích hợp 
#define MAX30102_REG_TEMP_INT       0x1F    // Phần nguyên của giá trị nhiệt độ (độ C)
                                            // 
#define MAX30102_REG_TEMP_FRAC      0x20    // Phần thập phân của giá trị nhiệt độ
#define MAX30102_REG_TEMP_CONFIG    0x21    // Kích hoạt đo nhiệt độ (tự động tắt sau khi đo xong)
// Định danh phần cứng 
#define MAX30102_REG_REV_ID         0xFE    // Mã phiên bản chip (Revision ID)
#define MAX30102_REG_PART_ID        0xFF    // Mã loại chip (Part ID)


// bit masks / Values cho các thiết lập cấu hình
// ============================================
// Mode Configuration Register (0x09) - bit masks
// ============================================
#define MAX30102_MODE_SHDN (1 << 7) // bit shutdown (tiết kiệm điện)
#define MAX30102_MODE_RESET (1<<6) // bit để reset, khởi động lại toàn bộ thanh ghi
#define MAX30102_MODE_HR 0x02 // chế độ đo nhịp tim (led đỏ)
#define MAX30102_MODE_SPO2 0x03 // chế độ đo SpO2 (nhịp tim + oxy) : led đỏ + hồng ngoại 
#define MAX30102_MODE_MULTI_LED 0x07 // chế độ đo nhiều LED (nhịp tim + SpO2)

// ============================================
// SpO2 Configuration Register (0x0A) - bit masks
// ============================================
// ADC Range (bits 6-5)
#define MAX30102_SPO2_ADC_RGE_2038nA     (0x00 << 5) 
#define MAX30102_SPO2_ADC_RGE_4096nA     (0x01 << 5) // Dải ADC tối đa 4096nA
#define MAX30102_SPO2_ADC_RGE_8192nA     (0x02 << 5) 
#define MAX30102_SPO2_ADC_RGE_16384nA    (0x03 << 5) 
// Sample Rate (bits 4-2)
#define MAX30102_SPO2_SR_50             (0x00 << 2)  // 50 samples/sec
#define MAX30102_SPO2_SR_100            (0x01 << 2)  // tốc độ lấy mẫu 100 SPS
#define MAX30102_SPO2_SR_200            (0x02 << 2)  // 200 samples/sec 
#define MAX30102_SPO2_SR_400            (0x03 << 2)  // 400 samples/sec 
#define MAX30102_SPO2_SR_800            (0x04 << 2)  // 800 samples/sec 
#define MAX30102_SPO2_SR_1000           (0x05 << 2)  // 1000 samples/sec 
#define MAX30102_SPO2_SR_1600           (0x06 << 2)  // 1600 samples/sec 
#define MAX30102_SPO2_SR_3200           (0x07 << 2)  // 3200 samples/sec 
// LED Pulse Width & ADC Resolution (bits 1-0)
#define MAX30102_SPO2_PW_69us_15b       0x00        // 69us, 15-bit 
#define MAX30102_SPO2_PW_118us_16b      0x01        // 118us, 16-bit 
#define MAX30102_SPO2_PW_215us_17b      0x02        // 215us, 17-bit 
#define MAX30102_SPO2_PW_411us_18b      0x03        // độ rộng xung LED 411us (tương đương với độ phân giải ADC 4096)

#define MAX30102_SPO2_DEFAULT_CONFIG (MAX30102_SPO2_ADC_RGE_4096nA | MAX30102_SPO2_SR_100 | MAX30102_SPO2_PW_411us_18b)

// ============================================
// FIFO Configuration Register (0x08) - bit masks
// ============================================
// Sample Averaging (bits 7-5): số lượng lấy mẫu mỗi lần 
#define MAX30102_FIFO_AVE_1             (0x00 << 5)
#define MAX30102_FIFO_AVE_2             (0x01 << 5)
#define MAX30102_FIFO_AVE_4             (0x02 << 5) // Nếu chọn AVE_4, cảm biến sẽ lấy 4 mẫu liên tiếp, cộng lại chia 4, rồi mới lưu 1 kết quả duy nhất vào FIFO.
#define MAX30102_FIFO_AVE_8             (0x03 << 5)
#define MAX30102_FIFO_AVE_16            (0x04 << 5)
#define MAX30102_FIFO_AVE_32            (0x05 << 5)
// Rollover Enable (bit 4): Cho phép ghi đè khi FIFO đầy
#define MAX30102_FIFO_ROLLOVER_EN       (1 << 4)   // Cho phép ghi đè khi FIFO đầy
#define MAX30102_FIFO_ROLLOVER_DIS      0           // Không cho phép ghi đè, sẽ ngừng ghi khi FIFO đầy
// FIFO Almost Full Threshold (bits 3-0): Ngưỡng báo động gần đầy
#define MAX30102_FIFO_ALMOST_FULL_VAL_8 0x08         // 4-bit chạy từ b0,1,2,3: số mẫu còn trống để trigger interrupt : "ngưỡng báo động" để tối ưu hóa việc đọc dữ liệu
#define MAX30102_FIFO_DEFAULT_CONFIG    (MAX30102_FIFO_AVE_1 | MAX30102_FIFO_ROLLOVER_EN | MAX30102_FIFO_ALMOST_FULL_VAL_8) // cấu hình mặc định: không trung bình mẫu, cho phép ghi đè khi đầy, ngưỡng gần đầy là 8 mẫu còn trống (tương đương với 24 byte dữ liệu, vì mỗi mẫu có 3 byte cho Red và 3 byte cho IR)

// ============================================
// Interrupt Registers (0x00-0x03) - bit masks
// ============================================
// Interrupt Status 1 & Enable 1
#define MAX30102_INT_A_FULL             (1 << 7)   // FIFO gần đầy
#define MAX30102_INT_PPG_RDY            (1 << 6)   // Dữ liệu mới sẵn sàng
#define MAX30102_INT_ALC_OVF            (1 << 5)   // Ambient Light Cancellation overflow
#define MAX30102_INT_PROX               (1 << 4)   // Proximity threshold reached
#define MAX30102_INT_PWR_RDY            (1 << 0)   // Power ready
// Interrupt Status 2 & Enable 2
#define MAX30102_INT_DIE_TEMP_RDY       (1 << 1)   // Nhiệt độ sẵn sàng



// ============================================
// Multi-LED Mode Control (0x11, 0x12)
// ============================================
#define MAX30102_SLOT_DISABLED          0x00
#define MAX30102_SLOT_LED1_RED          0x01       // Dùng LED1_PA
#define MAX30102_SLOT_LED2_IR           0x02       // Dùng LED2_PA
#define MAX30102_SLOT_LED1_PILOT        0x05       // Dùng PILOT_PA (Red)
#define MAX30102_SLOT_LED2_PILOT        0x06       // Dùng PILOT_PA (IR)

// ============================================
// LED Current (0x0C, 0x0D, 0x10)
// ============================================
#define MAX30102_LED_CURRENT(mA)        ((uint8_t)((mA) * 5.1f))  // Công thức gần đúng: 0.2mA/step
#define MAX30102_LED_CURRENT_7MA        0x24        // ~7.2mA
#define MAX30102_LED_CURRENT_15MA       0x4F        // ~15mA
#define MAX30102_LED_CURRENT_25MA       0x7F        // ~25mA
#define MAX30102_LED_CURRENT_50MA       0xFF        // 50mA max

// ============================================
// Temperature Configuration (0x21)
// ============================================
#define MAX30102_TEMP_EN                (1 << 0)














// ============================================
// Định nghĩa cấu trúc dữ liệu cho mẫu đọc được từ FIFO
// ============================================

/**
 * @brief Cấu trúc lưu trữ dữ liệu thô thu được từ bộ đệm FIFO
 */
typedef struct {
    uint32_t red; /**< Giá trị thô của LED Đỏ (Phản ánh lượng máu hấp thụ ánh sáng Đỏ) */
    uint32_t ir;  /**< Giá trị thô của LED Hồng ngoại (Phản ánh lượng máu hấp thụ ánh sáng IR) */
} max30102_sample_t;
// ============================================
// Cấu hình driver
// ============================================
typedef struct{
    uint8_t mode;              // chế độ đo: mode_hr, mode_spo2, mode_multi_led
    uint8_t adc_range;         // dải adc: adc range mask 
    uint8_t sample_rate;       // tốc độ lấy mẫu: sample rate mask 
    uint8_t pulse_width;       // độ rộng xung led: pulse width mask 
    uint8_t led_current_red;   // dòng led đỏ
    uint8_t led_current_ir;    // dòng led IR hồng ngoại 
    uint8_t fifo_almost_full;  // ngưỡng báo ngắt FIFO- số mẫu còn trống để trigger interrupt 
    bool enable_fifo_rollover; // cho phép ghi đè fifo
} max30102_config_t; // cấu trúc lưu trữ cấu hình của cảm biến MAX30102, bao gồm chế độ đo, dải ADC, tốc độ lấy mẫu, độ rộng xung LED, cường độ dòng điện cho LED đỏ và hồng ngoại, ngưỡng báo ngắt FIFO và tùy chọn cho phép ghi đè FIFO khi đầy


typedef struct{
    i2c_master_dev_handle_t i2c_dev_handle; // handle cho giao tiếp I2C với cảm biến
    max30102_config_t config;               // cấu hình hiện tại của cảm biến
    bool is_initialized;                    // trạng thái đã được khởi tạo hay chưa
} max30102_dev_t; // cấu trúc lưu trữ thông tin về thiết bị MAX30102, bao gồm handle I2C, cấu hình hiện tại và trạng thái khởi tạo
















// ============================================
// Function prototypes (Bản chuẩn hóa driver)
// ============================================

/**
 * @brief Khởi tạo cảm biến MAX30102
 */
esp_err_t max30102_init(max30102_dev_t *dev, i2c_master_bus_handle_t i2c_bus_handle, const max30102_config_t *config);

/**
 * @brief Reset phần cứng cảm biến về trạng thái mặc định
 */
esp_err_t max30102_reset(max30102_dev_t *dev); 

/**
 * @brief Kiểm tra định danh Part ID của chip xem có đúng là MAX30102 không
 */
esp_err_t max30102_check_part_id(max30102_dev_t *dev, bool *is_match);

/**
 * @brief Đọc 1 mẫu duy nhất từ FIFO (Dùng khi đọc liên tục hoặc lấy mẫu chậm)
 */
esp_err_t max30102_read_sample(max30102_dev_t *dev, max30102_sample_t *sample);

/**
 * @brief Đọc nhiều mẫu từ bộ đệm FIFO (Burst Read - Khuyên dùng khi xử lý ngắt)
 */
esp_err_t max30102_read_samples(max30102_dev_t *dev, max30102_sample_t *samples, uint8_t count, uint8_t *samples_read);

/**
 * @brief Lấy số lượng mẫu dữ liệu hiện tại đang có sẵn trong bộ đệm FIFO
 */
esp_err_t max30102_get_available_samples(max30102_dev_t *dev, uint8_t *count);

/**
 * @brief Đọc giá trị nhiệt độ lõi của cảm biến
 */
esp_err_t max30102_read_temperature(max30102_dev_t *dev, float *temperature);

/**
 * @brief Thay đổi động dòng điện cấp cho LED Đỏ và LED Hồng Ngoại
 */
esp_err_t max30102_set_led_current(max30102_dev_t *dev, float red_ma, float ir_ma); 

/**
 * @brief Thay đổi động chế độ hoạt động (HR Mode, SpO2 Mode, Multi-LED Mode)
 */
esp_err_t max30102_set_mode(max30102_dev_t *dev, uint8_t mode);

/**
 * @brief Thay đổi động tốc độ lấy mẫu (Sample Rate)
 */
esp_err_t max30102_set_sample_rate(max30102_dev_t *dev, uint8_t sample_rate);

/**
 * @brief Bật hoặc tắt chế độ tiết kiệm năng lượng (Shutdown)
 */
esp_err_t max30102_shutdown(max30102_dev_t *dev, bool enable);

/**
 * @brief Đọc các thanh ghi trạng thái ngắt (Xóa cờ ngắt sau khi đọc)
 */
esp_err_t max30102_get_interrupt_status(max30102_dev_t *dev, uint8_t *status1, uint8_t *status2);

/**
 * @brief Đọc vị trí các con trỏ FIFO và bộ đếm tràn
 */
esp_err_t max30102_get_fifo_status(max30102_dev_t *dev, uint8_t *wr_ptr, uint8_t *rd_ptr, uint8_t *overflow);

/**
 * @brief Xóa sạch bộ đệm FIFO (Đưa các con trỏ ghi/đọc về 0)
 */
esp_err_t max30102_clear_fifo(max30102_dev_t *dev);

/**
 * @brief Cấu hình chế độ phát hiện tiệm cận vật thể (Proximity Mode)
 */
esp_err_t max30102_configure_proximity(max30102_dev_t *dev, uint8_t threshold, float pilot_current);

#ifdef __cplusplus // nếu đang biên dịch với C thông thường thì không cần extern "C"
}
#endif 

