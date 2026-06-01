const crypto = require('crypto');
const jwt = require('jsonwebtoken');
const User = require('../models/User');
const {
  JWT_SECRET,
  JWT_EXPIRES_IN,
  REFRESH_TOKEN_SECRET,
  REFRESH_TOKEN_EXPIRES_IN,
  DEFAULT_ADMIN_EMAIL,
  DEFAULT_ADMIN_PASSWORD
} = require('../config/auth');

const createDefaultAdmin = async () => {
  try {
    const adminExists = await User.findOne({ email: DEFAULT_ADMIN_EMAIL });
    if (!adminExists) {
      const adminUser = await User.create({
        email: DEFAULT_ADMIN_EMAIL,
        password: DEFAULT_ADMIN_PASSWORD,
        fullName: 'Quản trị viên',
        phone: '0123456789',
        address: 'Hà Nội',
        role: 'admin'
      });
      console.log(`[Auth] Default admin account created: ${DEFAULT_ADMIN_EMAIL} / ${DEFAULT_ADMIN_PASSWORD}`);
    } else {
      // Verify admin user exists and password is correct
      const isValid = await adminExists.comparePassword(DEFAULT_ADMIN_PASSWORD);
      if (!isValid) {
        // Delete old user and create new one to avoid double hashing
        await User.deleteOne({ email: DEFAULT_ADMIN_EMAIL });
        await User.create({
          email: DEFAULT_ADMIN_EMAIL,
          password: DEFAULT_ADMIN_PASSWORD,
          fullName: 'Quản trị viên',
          phone: '0123456789',
          address: 'Hà Nội',
          role: 'admin'
        });
        console.log(`[Auth] Admin user recreated with default password: ${DEFAULT_ADMIN_EMAIL} / ${DEFAULT_ADMIN_PASSWORD}`);
      } else {
        console.log(`[Auth] Default admin account already exists and password is valid`);
      }
    }
  } catch (error) {
    console.error(`[Auth] Error creating default admin: ${error.message}`);
  }
};

const generateToken = (user) => {
  return jwt.sign(
    {
      id: user._id,
      email: user.email,
      role: user.role
    },
    JWT_SECRET,
    { expiresIn: JWT_EXPIRES_IN }
  );
};

const generateRefreshToken = (userId, tokenId) => {
  return jwt.sign(
    {
      id: userId,
      tokenId,
      type: 'refresh'
    },
    REFRESH_TOKEN_SECRET,
    { expiresIn: REFRESH_TOKEN_EXPIRES_IN }
  );
};

const authenticate = async (email, password) => {
  try {
    const user = await User.findOne({ email });
    
    if (!user) {
      console.log(`[Auth] User not found: ${email}`);
      return null;
    }

    const isValid = await user.comparePassword(password);
    if (!isValid) {
      console.log(`[Auth] Password mismatch for user: ${email}`);
      return null;
    }

    console.log(`[Auth] User authenticated: ${email}`);
    return user;
  } catch (error) {
    console.error(`[Auth] Authentication error: ${error.message}`);
    return null;
  }
};

const registerUser = async ({ email, password, fullName, phone, address, role = 'user' }) => {
  const existingUser = await User.findOne({ email });
  if (existingUser) {
    throw new Error('Email đã tồn tại');
  }

  const user = new User({ email, password, fullName, phone, address, role });
  return user.save();
};

const createRefreshTokenForUser = async (user) => {
  const tokenId = crypto.randomBytes(32).toString('hex');
  user.refreshTokens.push(tokenId);
  await user.save();
  return generateRefreshToken(user._id.toString(), tokenId);
};

const verifyRefreshToken = async (token) => {
  try {
    const decoded = jwt.verify(token, REFRESH_TOKEN_SECRET);
    if (decoded.type !== 'refresh') {
      return null;
    }

    const user = await User.findById(decoded.id);
    if (!user) {
      return null;
    }

    if (!user.refreshTokens.includes(decoded.tokenId)) {
      return null;
    }

    return { user, tokenId: decoded.tokenId };
  } catch (err) {
    return null;
  }
};

const rotateRefreshToken = async (user, oldTokenId) => {
  user.refreshTokens = user.refreshTokens.filter((id) => id !== oldTokenId);
  return createRefreshTokenForUser(user);
};

const revokeRefreshToken = async (refreshToken) => {
  const data = await verifyRefreshToken(refreshToken);
  if (!data) {
    return false;
  }

  const { user, tokenId } = data;
  user.refreshTokens = user.refreshTokens.filter((id) => id !== tokenId);
  await user.save();
  return true;
};

module.exports = {
  createDefaultAdmin,
  generateToken,
  authenticate,
  registerUser,
  createRefreshTokenForUser,
  verifyRefreshToken,
  rotateRefreshToken,
  revokeRefreshToken
};
