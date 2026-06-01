import React from 'react';

const AuthLayout = ({ children }) => {
  return (
    <div className="auth-container">
      {/* We can add a common header or background elements for the auth layout here in the future */}
      {children}
    </div>
  );
};

export default AuthLayout;
