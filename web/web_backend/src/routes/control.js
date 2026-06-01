const express = require('express');
const router = express.Router();
const { sendControlCommand } = require('../controllers/controlController');
const { authenticateUser } = require('../middleware/authMiddleware');

router.post('/', authenticateUser, sendControlCommand);

module.exports = router;
