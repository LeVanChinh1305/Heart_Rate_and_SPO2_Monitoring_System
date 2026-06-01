const mqttService = require('../services/mqttService');

const sendControlCommand = async (req, res) => {
  const { command } = req.body;

  if (!command) {
    return res.status(400).json({ success: false, message: 'Thiếu lệnh điều khiển' });
  }

  try {
    await mqttService.publishControlCommand(command);
    console.log(`[MQTT] Published control command: ${command}`);
    return res.status(200).json({ success: true, message: `Đã gửi lệnh: ${command}` });
  } catch (err) {
    console.error('[MQTT] Control publish error:', err);
    return res.status(500).json({ success: false, message: 'Gửi lệnh MQTT thất bại' });
  }
};

module.exports = {
  sendControlCommand
};
