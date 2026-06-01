const authService = require('../services/authService');
const User = require('../models/User');

const getRefreshCookieOptions = () => ({
  httpOnly: true,
  secure: process.env.NODE_ENV === 'production',
  sameSite: 'strict',
  path: '/api/auth',
  maxAge: 7 * 24 * 60 * 60 * 1000 // 7 days
});

const login = async (req, res) => {
  const { email, password } = req.body;

  if (!email || !password) {
    return res.status(400).json({ success: false, message: 'Vui lòng cung cấp email và mật khẩu' });
  }

  try {
    const user = await authService.authenticate(email, password);
    if (!user) {
      return res.status(401).json({ success: false, message: 'Email hoặc mật khẩu không đúng' });
    }

    const token = authService.generateToken(user);
    const refreshToken = await authService.createRefreshTokenForUser(user);

    res.cookie('refreshToken', refreshToken, getRefreshCookieOptions());

    return res.status(200).json({
      success: true,
      message: 'Đăng nhập thành công',
      token,
      user: {
        email: user.email,
        fullName: user.fullName,
        phone: user.phone,
        address: user.address,
        avatar: user.avatar,
        role: user.role
      }
    });
  } catch (error) {
    return res.status(500).json({ success: false, message: 'Lỗi máy chủ nội bộ' });
  }
};

const refreshToken = async (req, res) => {
  const { refreshToken } = req.cookies || {};
  if (!refreshToken) {
    return res.status(401).json({ success: false, message: 'Refresh token không được cung cấp' });
  }

  const data = await authService.verifyRefreshToken(refreshToken);
  if (!data) {
    return res.status(401).json({ success: false, message: 'Refresh token không hợp lệ' });
  }

  const { user, tokenId } = data;
  const newRefreshToken = await authService.rotateRefreshToken(user, tokenId);
  const token = authService.generateToken(user);

  res.cookie('refreshToken', newRefreshToken, getRefreshCookieOptions());

  return res.status(200).json({
    success: true,
    message: 'Refresh token thành công',
    token
  });
};

const logout = async (req, res) => {
  const { refreshToken } = req.cookies || {};
  if (refreshToken) {
    await authService.revokeRefreshToken(refreshToken);
  }
  res.clearCookie('refreshToken', { path: '/api/auth' });
  return res.status(200).json({ success: true, message: 'Đăng xuất thành công' });
};

const addUser = async (req, res) => {
  const { email, password, fullName, phone, address } = req.body;

  if (!email || !password || !fullName || !phone) {
    return res.status(400).json({ success: false, message: 'Vui lòng điền đủ email, mật khẩu, họ tên và SĐT' });
  }

  try {
    await authService.registerUser({ email, password, fullName, phone, address });
    return res.status(201).json({ success: true, message: 'Thêm người dùng thành công' });
  } catch (error) {
    return res.status(400).json({ success: false, message: error.message || 'Lỗi máy chủ nội bộ' });
  }
};

const updateProfile = async (req, res) => {
  const { fullName, address, phone, avatar } = req.body;
  const user = req.user;

  if (!user) {
    return res.status(401).json({ success: false, message: 'Không có quyền truy cập' });
  }

  try {
    if (fullName !== undefined) user.fullName = fullName;
    if (address !== undefined) user.address = address;
    if (phone !== undefined) user.phone = phone;
    if (avatar !== undefined) user.avatar = avatar;

    await user.save();

    return res.status(200).json({
      success: true,
      message: 'Cập nhật tài khoản thành công',
      user: {
        email: user.email,
        fullName: user.fullName,
        phone: user.phone,
        address: user.address,
        avatar: user.avatar,
        role: user.role
      }
    });
  } catch (error) {
    return res.status(500).json({ success: false, message: 'Lỗi máy chủ nội bộ' });
  }
};

const getUsers = async (req, res) => {
  try {
    const users = await User.find({}, 'email fullName phone role');
    return res.status(200).json({ success: true, users });
  } catch (error) {
    return res.status(500).json({ success: false, message: 'Lỗi máy chủ nội bộ' });
  }
};

module.exports = {
  login,
  refreshToken,
  logout,
  addUser,
  updateProfile,
  getUsers
};
