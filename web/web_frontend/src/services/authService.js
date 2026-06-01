// Auth Service - Manages authentication tokens and API calls
const API_URL = "http://localhost:5000/api/auth";

// Store token in memory (cleared on page reload)
let accessToken = null;

// Set token after login
export const setAccessToken = (token) => {
  accessToken = token;
};

// Get current token
export const getAccessToken = () => {
  return accessToken;
};

// Clear token on logout
export const clearAccessToken = () => {
  accessToken = null;
};

// Fetch with automatic Authorization header and token refresh
export const authFetch = async (url, options = {}) => {
  // Add Authorization header if token exists and it's an API call
  const headers = {
    "Content-Type": "application/json",
    ...options.headers,
  };

  if (accessToken && url.startsWith("http://localhost:5000/api")) {
    headers.Authorization = `Bearer ${accessToken}`;
  }

  let response = await fetch(url, {
    ...options,
    headers,
    credentials: "include", // Important: Send cookies with every request
  });

  // If 401, try to refresh token
  if (response.status === 401 && url !== `${API_URL}/refresh`) {
    const refreshed = await refreshAccessToken();
    if (refreshed) {
      // Retry original request with new token
      headers.Authorization = `Bearer ${accessToken}`;
      response = await fetch(url, {
        ...options,
        headers,
        credentials: "include",
      });
    }
  }

  return response;
};

// Login API
export const login = async (email, password) => {
  try {
    const response = await fetch(`${API_URL}/login`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ email, password }),
      credentials: "include", // Send cookies
    });

    const data = await response.json();

    if (response.ok && data.success) {
      setAccessToken(data.token); // Store token from response
      return { success: true, user: data.user };
    }

    return { success: false, message: data.message || "Đăng nhập thất bại" };
  } catch (error) {
    console.error("Login fetch error:", error);
    return { success: false, message: "Không thể kết nối đến máy chủ" };
  }
};

// Refresh token API
export const refreshAccessToken = async () => {
  try {
    const response = await fetch(`${API_URL}/refresh`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      credentials: "include", // Send refresh token cookie
    });

    const data = await response.json();

    if (response.ok && data.success) {
      setAccessToken(data.token); // Update access token
      return true;
    }

    // If refresh fails, clear token and redirect to login
    clearAccessToken();
    return false;
  } catch (error) {
    console.error("Token refresh error:", error);
    clearAccessToken();
    return false;
  }
};

// Logout API
export const logout = async () => {
  try {
    const response = await authFetch(`${API_URL}/logout`, {
      method: "POST",
    });

    if (response.ok) {
      clearAccessToken();
      return { success: true };
    }

    return { success: false, message: "Đăng xuất thất bại" };
  } catch (error) {
    console.error("Logout error:", error);
    clearAccessToken(); // Clear local token even if API call fails
    return { success: false, message: error.message };
  }
};

// Get user profile (Get current user from JWT)
// Note: Profile info is already in user state from login response
// This is a placeholder if you need to fetch fresh user data later
export const getUserProfile = async () => {
  try {
    // Since we don't have a /profile endpoint, 
    // user data comes from login response
    // You can implement this later if needed
    return { success: true, user: null };
  } catch (error) {
    console.error("Get profile error:", error);
    return { success: false, message: error.message };
  }
};

// Update user profile
export const updateProfile = async (userData) => {
  const response = await authFetch(`${API_URL}/update-profile`, {
    method: "POST",
    body: JSON.stringify(userData),
  });

  const data = await response.json();

  if (response.ok) {
    return { success: true, user: data.user };
  }

  return { success: false, message: data.message || "Cập nhật thông tin thất bại" };
};
