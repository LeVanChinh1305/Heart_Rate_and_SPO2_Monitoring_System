const mqtt = require('mqtt');
const { MQTT_BROKER, MQTT_TOPIC_DATA, MQTT_TOPIC_RAW, MQTT_TOPIC_CONTROL } = require('../config/mqtt');
const healthDataService = require('./healthDataService');

let mqttClient = null;

const initMqttClient = (onDataMessage) => {
  mqttClient = mqtt.connect(MQTT_BROKER);

  mqttClient.on('connect', () => {
    console.log(`[MQTT] Connected to broker: ${MQTT_BROKER}`);
    mqttClient.subscribe(MQTT_TOPIC_DATA, { qos: 1 }, (err) => {
      if (!err) {
        console.log(`[MQTT] Subscribed to: ${MQTT_TOPIC_DATA}`);
      }
    });
    mqttClient.subscribe(MQTT_TOPIC_RAW, { qos: 0 }, (err) => {
      if (!err) {
        console.log(`[MQTT] Subscribed to: ${MQTT_TOPIC_RAW}`);
      }
    });
  });

  mqttClient.on('message', async (topic, message) => {
    try {
      const payload = JSON.parse(message.toString());

      if (topic === MQTT_TOPIC_DATA) {
        console.log('[MQTT] Received:', payload);
        onDataMessage(payload);

        try {
          await healthDataService.saveHealthData(payload);
          console.log('[MongoDB] Health data saved');
        } catch (err) {
          console.error('[MongoDB] Save error:', err);
        }
      } else if (topic === MQTT_TOPIC_RAW) {
        console.log('[MQTT] Received RAW:', payload);
        onDataMessage({ raw_hr: payload.val });
      }
    } catch (err) {
      console.error('[MQTT] Invalid JSON:', message.toString());
    }
  });

  mqttClient.on('error', (err) => {
    console.error('[MQTT] Connection error:', err.message);
  });
};

const publishControlCommand = (command) => {
  return new Promise((resolve, reject) => {
    if (!mqttClient) {
      return reject(new Error('MQTT client is not initialized'));
    }

    mqttClient.publish(MQTT_TOPIC_CONTROL, command, { qos: 1 }, (err) => {
      if (err) {
        return reject(err);
      }
      resolve();
    });
  });
};

module.exports = {
  initMqttClient,
  publishControlCommand
};
