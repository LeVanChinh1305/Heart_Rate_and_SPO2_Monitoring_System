const express = require('express');
const router = express.Router();
const authController = require('../controllers/authController');
const { authenticateUser, requireAdmin } = require('../middleware/authMiddleware');

router.post('/login', authController.login);
router.post('/refresh', authController.refreshToken);
router.post('/logout', authController.logout);
router.post('/add-user', authenticateUser, requireAdmin, authController.addUser);
router.post('/update-profile', authenticateUser, authController.updateProfile);
router.get('/users', authenticateUser, requireAdmin, authController.getUsers);

module.exports = router;
