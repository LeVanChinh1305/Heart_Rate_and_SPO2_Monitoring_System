const mongoose = require('mongoose');

const healthDataSchema = new mongoose.Schema({
  bpm: {
    type: Number,
    required: true
  },
  spo2: {
    type: Number,
    required: true
  },
  body_temp: {
    type: Number,
    required: true
  },
  deviceId: {
    type: String,
    default: ''
  }
}, { timestamps: true });

module.exports = mongoose.model('HealthData', healthDataSchema);
