import React from 'react';

const GuidePage = () => {
  return (
    <div className="card" style={{ maxWidth: '800px', margin: '0 auto', textAlign: 'left' }}>
      <h2 style={{ textAlign: 'center', color: 'var(--accent)', marginBottom: '1.5rem' }}>Hướng dẫn sử dụng</h2>
      
      <div style={{ lineHeight: '1.6', color: 'var(--text-color)' }}>
        <h3 style={{ marginBottom: '0.5rem' }}>1. Đo nhịp tim và SpO2</h3>
        <p style={{ marginBottom: '1rem' }}>
          Đeo thiết bị vào cổ tay, đảm bảo cảm biến tiếp xúc tốt với da. Các chỉ số Nhịp tim và nồng độ oxy trong máu (SpO2) 
          sẽ tự động được cập nhật trên màn hình thiết bị và đồng bộ lên hệ thống.
        </p>

        <h3 style={{ marginBottom: '0.5rem' }}>2. Cập nhật thông tin cá nhân</h3>
        <p style={{ marginBottom: '1rem' }}>
          Bấm vào biểu tượng người dùng ở góc phải trên cùng, chọn "Tài khoản cá nhân". 
          Tại đây bạn có thể thay đổi Họ tên, Số điện thoại, Địa chỉ và URL Ảnh đại diện.
        </p>

        <h3 style={{ marginBottom: '0.5rem' }}>3. Quản trị hệ thống (Dành cho Admin)</h3>
        <p style={{ marginBottom: '1rem' }}>
          Quản trị viên có thể truy cập mục "Quản trị" trên thanh điều hướng để thêm người dùng mới,
          cũng như xem danh sách tất cả các tài khoản hiện có trong hệ thống.
        </p>
      </div>
    </div>
  );
};

export default GuidePage;
