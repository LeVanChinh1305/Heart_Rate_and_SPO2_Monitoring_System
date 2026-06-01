require('dotenv').config();
const express = require('express');
const cors = require('cors');
const cookieParser = require('cookie-parser');
const http = require('http');
const { WebSocketServer } = require('ws');
const connectDatabase = require('./config/db');
const authService = require('./services/authService');
const mqttService = require('./services/mqttService');
const authRoutes = require('./routes/auth');
const controlRoutes = require('./routes/control');

const app = express();
const PORT = process.env.PORT || 5000;
const MONGO_URI = process.env.MONGO_URI || 'mongodb://127.0.0.1:27017/heart_spo2_db';

const corsOptions = {
  origin: ['http://localhost:5173', 'http://localhost:5174', 'http://localhost:3000'],
  credentials: true
};

// Middleware
app.use(cors(corsOptions));
app.use(cookieParser());
app.use(express.json());

// Routes
app.use('/api/auth', authRoutes);
app.use('/api/control', controlRoutes);

// ─── Create HTTP server & WebSocket server ───
const server = http.createServer(app);
const wss = new WebSocketServer({ server });

wss.on('connection', (ws) => {
  console.log('[WebSocket] Frontend client connected');
  ws.on('close', () => console.log('[WebSocket] Frontend client disconnected'));
});

// Broadcast helper – send data to all connected frontend clients
const broadcastToClients = (data) => {
  const payload = JSON.stringify(data);
  wss.clients.forEach((client) => {
    if (client.readyState === 1) { // WebSocket.OPEN
      client.send(payload);
    }
  });
};

// ─── Startup
connectDatabase(MONGO_URI)
  .then(async () => {
    console.log('MongoDB connected successfully');
    await authService.createDefaultAdmin();
    mqttService.initMqttClient(broadcastToClients);

    server.listen(PORT, () => {
      console.log(`Server is running on port ${PORT}`);
      console.log(`WebSocket server ready on ws://localhost:${PORT}`);
    });
  })
  .catch((err) => {
    console.error('MongoDB connection error:', err);
    process.exit(1);
  });
