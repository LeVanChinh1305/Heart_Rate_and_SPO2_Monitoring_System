import React, { useState, useEffect } from 'react';
import { authFetch } from '../../services/authService';

const AdminPage = ({ user }) => {
  const [usersList, setUsersList] = useState([]);
  const [newEmail, setNewEmail] = useState('');
  const [newPassword, setNewPassword] = useState('');
  const [newFullName, setNewFullName] = useState('');
  const [newPhone, setNewPhone] = useState('');
  const [message, setMessage] = useState('');
  const [error, setError] = useState('');

  const fetchUsers = async () => {
    try {
      const response = await authFetch('http://localhost:5000/api/auth/users');
      const data = await response.json();
      if (data.success) {
        setUsersList(data.users);
      }
    } catch (err) {
      console.error("Lỗi khi lấy danh sách user:", err);
    }
  };

  useEffect(() => {
    fetchUsers();
  }, []);

  const handleAddUser = async (e) => {
    e.preventDefault();
    setMessage('');
    setError('');

    try {
      const response = await authFetch('http://localhost:5000/api/auth/add-user', {
        method: 'POST',
        body: JSON.stringify({
          requesterRole: user.role,
          email: newEmail,
          password: newPassword,
          fullName: newFullName,
          phone: newPhone
        })
      });

      const data = await response.json();
      
      if (response.ok && data.success) {
        setMessage(data.message);
        setNewEmail('');
        setNewPassword('');
        setNewFullName('');
        setNewPhone('');
        fetchUsers(); // Refresh the list
      } else {
        setError(data.message || 'Lỗi khi thêm người dùng');
      }
    } catch (err) {
      setError('Lỗi kết nối máy chủ');
    }
  };

  return (
    <div className="card" style={{ maxWidth: '900px', margin: '0 auto', textAlign: 'left' }}>
      <h2 style={{ textAlign: 'center', color: 'var(--accent)', marginBottom: '1.5rem' }}>Quản trị hệ thống</h2>
      
      <div style={{ display: 'flex', gap: '2rem', flexWrap: 'wrap' }}>
        {/* Form Add User */}
        <div style={{ flex: '1', minWidth: '300px' }}>
          <h3 style={{ marginBottom: '1rem', color: 'var(--text-color)' }}>Thêm người dùng mới</h3>
          <form onSubmit={handleAddUser}>
            <input 
              type="email" 
              placeholder="Email" 
              value={newEmail} 
              onChange={(e) => setNewEmail(e.target.value)} 
              required 
            />
            <input 
              type="password" 
              placeholder="Mật khẩu" 
              value={newPassword} 
              onChange={(e) => setNewPassword(e.target.value)} 
              required 
            />
            <input 
              type="text" 
              placeholder="Họ và tên" 
              value={newFullName} 
              onChange={(e) => setNewFullName(e.target.value)} 
              required 
            />
            <input 
              type="tel" 
              placeholder="Số điện thoại" 
              value={newPhone} 
              onChange={(e) => setNewPhone(e.target.value)} 
              required 
            />
            <button type="submit">Thêm tài khoản</button>
            {message && <p style={{ color: 'var(--accent)', marginTop: '0.5rem' }}>{message}</p>}
            {error && <p className="error">{error}</p>}
          </form>
        </div>

        {/* User List */}
        <div style={{ flex: '1.5', minWidth: '300px' }}>
          <h3 style={{ marginBottom: '1rem', color: 'var(--text-color)' }}>Danh sách tài khoản</h3>
          <div style={{ overflowX: 'auto' }}>
            <table className="admin-table">
              <thead>
                <tr>
                  <th>Email</th>
                  <th>Họ và tên</th>
                  <th>Số điện thoại</th>
                  <th>Vai trò</th>
                </tr>
              </thead>
              <tbody>
                {usersList.map((u, idx) => (
                  <tr key={idx}>
                    <td>{u.email}</td>
                    <td>{u.fullName}</td>
                    <td>{u.phone}</td>
                    <td>
                      <span className={`role-badge ${u.role === 'admin' ? 'role-admin' : 'role-user'}`}>
                        {u.role === 'admin' ? 'Quản trị' : 'Người dùng'}
                      </span>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      </div>
    </div>
  );
};

export default AdminPage;
