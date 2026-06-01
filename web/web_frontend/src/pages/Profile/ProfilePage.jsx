import React, { useState } from 'react';
import { authFetch } from '../../services/authService';

const ProfilePage = ({ user, onUpdateSuccess }) => {
  const [fullName, setFullName] = useState(user.fullName || '');
  const [phone, setPhone] = useState(user.phone || '');
  const [address, setAddress] = useState(user.address || '');
  const [avatar, setAvatar] = useState(user.avatar || '');
  
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');

  const handleUpdate = async (e) => {
    e.preventDefault();
    setMessage('');
    setError('');

    try {
      const response = await authFetch('http://localhost:5000/api/auth/update-profile', {
        method: 'POST',
        body: JSON.stringify({
          email: user.email,
          fullName,
          phone,
          address,
          avatar
        })
      });

      const data = await response.json();
      
      if (response.ok && data.success) {
        setMessage(data.message);
        if (onUpdateSuccess) {
          onUpdateSuccess(data.user);
        }
      } else {
        setError(data.message || 'Lỗi khi cập nhật tài khoản');
      }
    } catch (err) {
      setError('Lỗi kết nối máy chủ');
    }
  };

  return (
    <div className="card" style={{ maxWidth: '600px', margin: '0 auto', textAlign: 'left' }}>
      <h2 style={{ textAlign: 'center', color: 'var(--accent)', marginBottom: '1.5rem' }}>Tài khoản cá nhân</h2>
      
      <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', marginBottom: '1.5rem' }}>
        {avatar ? (
          <img 
            src={avatar} 
            alt="Avatar" 
            style={{ width: '100px', height: '100px', borderRadius: '50%', objectFit: 'cover', marginBottom: '1rem', border: '3px solid var(--accent)' }} 
          />
        ) : (
          <div style={{ width: '100px', height: '100px', borderRadius: '50%', backgroundColor: '#a5d6a7', display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: '2rem', color: '#fff', marginBottom: '1rem' }}>
            {fullName.charAt(0).toUpperCase()}
          </div>
        )}
        <p><strong>{user.email}</strong></p>
        <p className={`role-badge ${user.role === 'admin' ? 'role-admin' : 'role-user'}`} style={{ marginTop: '0.5rem' }}>
          {user.role === 'admin' ? 'Quản trị viên' : 'Người dùng'}
        </p>
      </div>

      <form onSubmit={handleUpdate}>
        <div style={{ marginBottom: '1rem' }}>
          <label style={{ display: 'block', marginBottom: '0.5rem', color: 'var(--text-color)' }}>Họ và tên</label>
          <input 
            type="text" 
            value={fullName} 
            onChange={(e) => setFullName(e.target.value)} 
            required 
          />
        </div>
        
        <div style={{ marginBottom: '1rem' }}>
          <label style={{ display: 'block', marginBottom: '0.5rem', color: 'var(--text-color)' }}>Số điện thoại</label>
          <input 
            type="tel" 
            value={phone} 
            onChange={(e) => setPhone(e.target.value)} 
          />
        </div>
        
        <div style={{ marginBottom: '1rem' }}>
          <label style={{ display: 'block', marginBottom: '0.5rem', color: 'var(--text-color)' }}>Địa chỉ</label>
          <input 
            type="text" 
            value={address} 
            onChange={(e) => setAddress(e.target.value)} 
          />
        </div>

        <div style={{ marginBottom: '1.5rem' }}>
          <label style={{ display: 'block', marginBottom: '0.5rem', color: 'var(--text-color)' }}>URL Ảnh đại diện (Avatar)</label>
          <input 
            type="url" 
            value={avatar} 
            placeholder="https://example.com/avatar.jpg"
            onChange={(e) => setAvatar(e.target.value)} 
          />
        </div>

        <button type="submit">Cập nhật thông tin</button>
        {message && <p style={{ color: 'var(--accent)', marginTop: '0.5rem', textAlign: 'center' }}>{message}</p>}
        {error && <p className="error" style={{ textAlign: 'center' }}>{error}</p>}
      </form>
    </div>
  );
};

export default ProfilePage;
