const HealthData = require('../models/HealthData');

const saveHealthData = async (healthData) => {
  const record = new HealthData({
    bpm: parseFloat(healthData.bpm) || 0,
    spo2: parseFloat(healthData.spo2) || 0,
    body_temp: parseFloat(healthData.body_temp) || 0,
    deviceId: healthData.deviceId || ''
  });

  return record.save();
};

module.exports = {
  saveHealthData
};
