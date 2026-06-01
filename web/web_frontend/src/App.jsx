import { useState, useEffect } from 'react';
import './index.css';
import { getAccessToken, clearAccessToken } from './services/authService';
import AuthLayout from './layouts/AuthLayout';
import MainLayout from './layouts/MainLayout';
import LoginPage from './pages/Login/LoginPage';
import HomePage from './pages/Home/HomePage';
import AdminPage from './pages/Admin/AdminPage';
import ProfilePage from './pages/Profile/ProfilePage';
import GuidePage from './pages/Guide/GuidePage';

function App() {
  const [user, setUser] = useState(null);
  const [currentView, setCurrentView] = useState('home');
  const [isInitialized, setIsInitialized] = useState(false);

  // Check if user is already logged in (token exists)
  useEffect(() => {
    const token = getAccessToken();
    if (token && !user) {
      // Token exists but user state is empty - This would only happen on page load
      // In this case, token would be lost (stored in memory only)
      // User would need to login again
    }
    setIsInitialized(true);
  }, [user]);

  const handleLoginSuccess = (userData) => {
    setUser(userData);
    setCurrentView('home');
  };

  const handleProfileUpdate = (updatedUserData) => {
    setUser(updatedUserData);
  };

  const handleLogout = () => {
    clearAccessToken();
    setUser(null);
    setCurrentView('home');
  };

  if (!user) {
    return (
      <AuthLayout>
        <LoginPage onLoginSuccess={handleLoginSuccess} />
      </AuthLayout>
    );
  }

  const renderView = () => {
    switch (currentView) {
      case 'admin':
        return user.role === 'admin' ? <AdminPage user={user} /> : <HomePage user={user} />;
      case 'profile':
        return <ProfilePage user={user} onUpdateSuccess={handleProfileUpdate} />;
      case 'guide':
        return <GuidePage />;
      case 'home':
      default:
        return <HomePage user={user} />;
    }
  };

  return (
    <MainLayout user={user} onLogout={handleLogout} onNavigate={setCurrentView} currentView={currentView}>
      {renderView()}
    </MainLayout>
  );
}

export default App;
