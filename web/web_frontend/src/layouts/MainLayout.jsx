import React, { useState, useRef, useEffect } from 'react';
import { User } from 'lucide-react';
import { logout } from '../services/authService';

const MainLayout = ({ children, user, onLogout, onNavigate, currentView }) => {
  const [dropdownOpen, setDropdownOpen] = useState(false);
  const [isLoggingOut, setIsLoggingOut] = useState(false);
  const dropdownRef = useRef(null);

  const toggleDropdown = () => setDropdownOpen(!dropdownOpen);

  // Close dropdown if clicked outside
  useEffect(() => {
    const handleClickOutside = (event) => {
      if (dropdownRef.current && !dropdownRef.current.contains(event.target)) {
        setDropdownOpen(false);
      }
    };
    document.addEventListener("mousedown", handleClickOutside);
    return () => {
      document.removeEventListener("mousedown", handleClickOutside);
    };
  }, []);

  const handleLogout = async () => {
    setDropdownOpen(false);
    setIsLoggingOut(true);

    try {
      const result = await logout();
      if (result.success || !result.success) {
        // Call onLogout regardless of API result (token is cleared locally)
        if (onLogout) onLogout();
      }
    } finally {
      setIsLoggingOut(false);
    }
  };

  const handleNavigate = (view) => {
    setDropdownOpen(false);
    if (onNavigate) onNavigate(view);
  };

  return (
    <div className="container">
      <header className="main-header">
        <h2 style={{ cursor: 'pointer' }} onClick={() => handleNavigate('home')}>Heart & SpO2 Monitor</h2>
        
        <div style={{ display: 'flex', alignItems: 'center' }}>
          <div className="nav-links">
            <button 
              className={`nav-link ${currentView === 'home' ? 'active' : ''}`}
              onClick={() => handleNavigate('home')}
            >
              Trang chủ
            </button>
            <button 
              className={`nav-link ${currentView === 'guide' ? 'active' : ''}`}
              onClick={() => handleNavigate('guide')}
            >
              Hướng dẫn
            </button>
            
            {user?.role === 'admin' && (
              <button 
                className={`nav-link ${currentView === 'admin' ? 'active' : ''}`}
                onClick={() => handleNavigate('admin')}
              >
                Quản trị
              </button>
            )}
          </div>

          <div className="user-menu-container" ref={dropdownRef}>
            <button className="user-icon-btn" onClick={toggleDropdown} title="Tài khoản">
              {user?.avatar ? (
                <img src={user.avatar} alt="Avatar" style={{ width: '100%', height: '100%', borderRadius: '50%', objectFit: 'cover' }} />
              ) : (
                <User size={22} />
              )}
            </button>
            
            {dropdownOpen && (
              <div className="dropdown-menu">
                <button 
                  className="dropdown-item" 
                  onClick={() => handleNavigate('profile')}
                >
                  Tài khoản cá nhân
                </button>
                <button className="dropdown-item logout" onClick={handleLogout} disabled={isLoggingOut}>
                  {isLoggingOut ? 'Đang đăng xuất...' : 'Đăng xuất'}
                </button>
              </div>
            )}
          </div>
        </div>
      </header>
      <main>
        {children}
      </main>
    </div>
  );
};

export default MainLayout;
