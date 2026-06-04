import React, { useState, useEffect, useRef } from 'react';
import { LineChart, Line, XAxis, YAxis, CartesianGrid, Tooltip, ResponsiveContainer, ReferenceLine } from 'recharts';
import { authFetch } from '../../services/authService';

const MAX_DATA_POINTS = 150;

const HomePage = ({ user }) => {
  const [data, setData] = useState([]);
  const [rawData, setRawData] = useState([]);
  const [thresholds, setThresholds] = useState({ hrMax: 140, spo2Min: 92 });
  const [powerSaving, setPowerSaving] = useState(false);
  const [wsConnected, setWsConnected] = useState(false);
  const [alerts, setAlerts] = useState([]);
  const wsRef = useRef(null);

  // Connect to WebSocket on mount
  useEffect(() => {
    const ws = new WebSocket('ws://localhost:5000');
    wsRef.current = ws;

    ws.onopen = () => {
      console.log('[WebSocket] Connected to server');
      setWsConnected(true);
    };

    ws.onmessage = (event) => {
      try {
        const incoming = JSON.parse(event.data);
        
        // Handle incoming alert
        if (incoming.alert !== undefined) {
          const newAlert = {
            id: Date.now() + Math.random(),
            time: new Date().toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' }),
            deviceId: incoming.deviceId || 'ESP32C6',
            message: incoming.alert
          };
          setAlerts(prev => [newAlert, ...prev].slice(0, 10));
        }
        // Handle raw heart rate data from chinh/health_raw topic
        else if (incoming.raw_hr !== undefined) {
          const rawPoint = {
            time: new Date().toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' }),
            value: parseInt(incoming.raw_hr) || 0
          };

          setRawData(prevData => {
            const newData = [...prevData, rawPoint];
            if (newData.length > MAX_DATA_POINTS) {
              return newData.slice(newData.length - MAX_DATA_POINTS);
            }
            return newData;
          });
        }
        // Handle aggregated data from chinh/health_data topic
        else if (incoming.bpm !== undefined) {
          const point = {
            time: new Date().toLocaleTimeString([], { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' }),
            hr: parseFloat(incoming.bpm) || 0,
            spo2: parseFloat(incoming.spo2) || 0,
            temp: parseFloat(incoming.body_temp) || 0
          };

          setData(prevData => {
            const newData = [...prevData, point];
            if (newData.length > MAX_DATA_POINTS) {
              return newData.slice(newData.length - MAX_DATA_POINTS);
            }
            return newData;
          });
        }
      } catch (e) {
        console.error('[WebSocket] Parse error:', e);
      }
    };

    ws.onclose = () => {
      console.log('[WebSocket] Disconnected');
      setWsConnected(false);
    };

    ws.onerror = (err) => {
      console.error('[WebSocket] Error:', err);
      setWsConnected(false);
    };

    return () => {
      ws.close();
    };
  }, []);

  // Send control command to device via backend REST API
  const sendCommand = async (command) => {
    try {
      const res = await authFetch('http://localhost:5000/api/control', {
        method: 'POST',
        body: JSON.stringify({ command })
      });
      const result = await res.json();
      if (result.success) {
        console.log('[Control]', result.message);
      } else {
        alert('Lỗi: ' + result.message);
      }
      return result;
    } catch (err) {
      alert('Không thể gửi lệnh tới máy chủ');
    }
  };

  const currentValues = data.length > 0 ? data[data.length - 1] : { hr: 0, spo2: 0, temp: 0 };

  const handleUpdateThresholds = async (e) => {
    e.preventDefault();
    await sendCommand(`SET_HR_LIMIT:${thresholds.hrMax}`);
    await sendCommand(`SET_SPO2_LIMIT:${thresholds.spo2Min}`);
    alert(`Đã gửi ngưỡng mới tới thiết bị!\nNhịp tim Max: ${thresholds.hrMax} BPM\nSpO2 Min: ${thresholds.spo2Min}%`);
  };

  const handleResetDevice = async () => {
    if (window.confirm("Bạn có chắc chắn muốn khởi động lại thiết bị không?")) {
      await sendCommand('REBOOT');
    }
  };

  const handlePowerSaving = async (checked) => {
    setPowerSaving(checked);
    if (checked) {
      if (window.confirm("Bật chế độ tiết kiệm năng lượng? Thiết bị sẽ vào chế độ ngủ sâu.")) {
        await sendCommand('DEEP_SLEEP');
      } else {
        setPowerSaving(false);
      }
    }
  };

  return (
    <div style={{ display: 'flex', width: '100%', gap: '1.5rem', flexWrap: 'nowrap', alignItems: 'flex-start' }}>
      
      {/* Main Content (Left) */}
      <div style={{ flex: '1', minWidth: '0' }}>

      {/* Connection Status */}
      <div style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', marginBottom: '1rem' }}>
        <span style={{
          width: '10px', height: '10px', borderRadius: '50%',
          background: wsConnected ? '#43a047' : '#d32f2f',
          display: 'inline-block'
        }}></span>
        <span style={{ fontSize: '0.85rem', color: 'var(--text-light)' }}>
          {wsConnected ? 'Đang kết nối tới thiết bị' : 'Mất kết nối – đang chờ dữ liệu...'}
        </span>
      </div>

      {/* Quick Stats Cards */}
      <div style={{ display: 'flex', gap: '1rem', marginBottom: '1.5rem', flexWrap: 'wrap' }}>
        <div className="card" style={{ flex: '1', minWidth: '200px', padding: '1.5rem 1rem' }}>
          <h3 style={{ fontSize: '1.1rem', color: 'var(--text-color)', marginBottom: '0.5rem' }}>Nhịp tim</h3>
          <p style={{ fontSize: '2rem', fontWeight: 'bold', color: '#e53935' }}>
            {currentValues.hr > 0 ? currentValues.hr.toFixed(1) : '--'} <span style={{ fontSize: '1rem', fontWeight: 'normal' }}>BPM</span>
          </p>
        </div>
        <div className="card" style={{ flex: '1', minWidth: '200px', padding: '1.5rem 1rem' }}>
          <h3 style={{ fontSize: '1.1rem', color: 'var(--text-color)', marginBottom: '0.5rem' }}>Nồng độ Oxy (SpO2)</h3>
          <p style={{ fontSize: '2rem', fontWeight: 'bold', color: '#1e88e5' }}>
            {currentValues.spo2 > 0 ? currentValues.spo2.toFixed(1) : '--'} <span style={{ fontSize: '1rem', fontWeight: 'normal' }}>%</span>
          </p>
        </div>
        <div className="card" style={{ flex: '1', minWidth: '200px', padding: '1.5rem 1rem' }}>
          <h3 style={{ fontSize: '1.1rem', color: 'var(--text-color)', marginBottom: '0.5rem' }}>Nhiệt độ cơ thể</h3>
          <p style={{ fontSize: '2rem', fontWeight: 'bold', color: '#fb8c00' }}>
            {currentValues.temp > 0 ? currentValues.temp.toFixed(1) : '--'} <span style={{ fontSize: '1rem', fontWeight: 'normal' }}>°C</span>
          </p>
        </div>
      </div>

      {/* Chart Section - Stacked vertically */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: '1rem', marginBottom: '1.5rem' }}>

        {/* Raw Heart Rate Chart */}
        <div className="card" style={{ width: '100%', padding: '1.5rem' }}>
          <h3 style={{ color: '#e53935', marginBottom: '1rem', textAlign: 'left' }}>Biểu đồ Nhịp Tim (Từ chinh/health_raw)</h3>
          <div style={{ height: '250px', width: '100%' }}>
            <ResponsiveContainer>
              <LineChart data={rawData} margin={{ top: 5, right: 20, left: -20, bottom: 5 }}>
                <CartesianGrid strokeDasharray="3 3" opacity={0.3} />
                <XAxis dataKey="time" stroke="var(--text-color)" fontSize={11} />
                <Tooltip contentStyle={{ backgroundColor: 'rgba(255, 255, 255, 0.9)', borderRadius: '8px' }} />
                <Line type="monotone" dataKey="value" name="Nhịp tim (Raw)" stroke="#e53935" strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

        {/* SpO2 Chart */}
        <div className="card" style={{ width: '100%', padding: '1.5rem' }}>
          <h3 style={{ color: '#1e88e5', marginBottom: '1rem', textAlign: 'left' }}>Biểu đồ SpO2 (%)</h3>
          <div style={{ height: '250px', width: '100%' }}>
            <ResponsiveContainer>
              <LineChart data={data} margin={{ top: 5, right: 20, left: -20, bottom: 5 }}>
                <CartesianGrid strokeDasharray="3 3" opacity={0.3} />
                <XAxis dataKey="time" stroke="var(--text-color)" fontSize={11} />
                <YAxis stroke="#1e88e5" domain={[85, 100]} fontSize={11} />
                <Tooltip contentStyle={{ backgroundColor: 'rgba(255, 255, 255, 0.9)', borderRadius: '8px' }} />
                <ReferenceLine y={Number(thresholds.spo2Min)} label="Nguy hiểm (Min)" stroke="#1976d2" strokeDasharray="3 3" />
                <Line type="monotone" dataKey="spo2" name="SpO2" stroke="#1e88e5" strokeWidth={2} dot={false} isAnimationActive={false} />
              </LineChart>
            </ResponsiveContainer>
          </div>
        </div>

      </div>

      {/* Admin Controls Section */}
      {user?.role === 'admin' && (
        <div className="card" style={{ padding: '1.5rem', textAlign: 'left' }}>
          <h3 style={{ color: '#e65100', marginBottom: '1rem' }}>Bảng điều khiển (Dành cho Quản trị viên)</h3>

          <div style={{ display: 'flex', gap: '2rem', flexWrap: 'wrap' }}>

            {/* Control Thresholds */}
            <div style={{ flex: '1', minWidth: '300px' }}>
              <h4 style={{ marginBottom: '1rem', color: 'var(--text-color)' }}>Cài đặt ngưỡng báo động</h4>
              <form onSubmit={handleUpdateThresholds} style={{ display: 'flex', gap: '1rem', alignItems: 'flex-end' }}>
                <div style={{ flex: '1' }}>
                  <label style={{ display: 'block', fontSize: '0.9rem', marginBottom: '0.3rem' }}>Nhịp tim Max (BPM)</label>
                  <input
                    type="number"
                    value={thresholds.hrMax}
                    onChange={(e) => setThresholds({ ...thresholds, hrMax: e.target.value })}
                    style={{ marginBottom: '0' }}
                  />
                </div>
                <div style={{ flex: '1' }}>
                  <label style={{ display: 'block', fontSize: '0.9rem', marginBottom: '0.3rem' }}>SpO2 Min (%)</label>
                  <input
                    type="number"
                    value={thresholds.spo2Min}
                    onChange={(e) => setThresholds({ ...thresholds, spo2Min: e.target.value })}
                    style={{ marginBottom: '0' }}
                  />
                </div>
                <button type="submit" style={{ flex: '1', background: '#fb8c00' }}>Cập nhật</button>
              </form>
            </div>

            {/* Device Controls */}
            <div style={{ flex: '1', minWidth: '300px' }}>
              <h4 style={{ marginBottom: '1rem', color: 'var(--text-color)' }}>Cấu hình thiết bị cứng</h4>
              <div style={{ display: 'flex', gap: '1rem', alignItems: 'center', marginBottom: '1rem' }}>
                <label style={{ display: 'flex', alignItems: 'center', cursor: 'pointer', gap: '0.5rem' }}>
                  <input
                    type="checkbox"
                    checked={powerSaving}
                    onChange={(e) => handlePowerSaving(e.target.checked)}
                    style={{ width: '20px', height: '20px', margin: 0 }}
                  />
                  <span>Bật chế độ tiết kiệm năng lượng (Deep Sleep)</span>
                </label>
              </div>
              <button
                onClick={handleResetDevice}
                style={{ background: '#d32f2f', width: 'auto', padding: '0.75rem 1.5rem' }}
              >
                Reset Thiết Bị Khẩn Cấp
              </button>
            </div>

          </div>
        </div>
      )}
      </div>

      {/* Notifications Panel (Right) */}
      <div style={{ width: '320px', flexShrink: 0, position: 'sticky', top: '1.5rem' }}>
        <div className="card" style={{ padding: '1.5rem', height: '100%', display: 'flex', flexDirection: 'column' }}>
          <h3 style={{ color: '#ff9800', marginBottom: '1rem', borderBottom: '1px solid #eee', paddingBottom: '0.5rem', textAlign: 'left' }}>
            🔔 Thông báo gần đây
          </h3>
          <div style={{ display: 'flex', flexDirection: 'column', gap: '0.75rem', overflowY: 'auto' }}>
            {alerts.length === 0 ? (
              <p style={{ color: 'var(--text-light)', fontStyle: 'italic', fontSize: '0.9rem', textAlign: 'left' }}>Chưa có thông báo nào.</p>
            ) : (
              alerts.map(alert => (
                <div key={alert.id} style={{ 
                  background: 'rgba(255, 152, 0, 0.1)', 
                  borderLeft: '4px solid #ff9800',
                  padding: '0.75rem',
                  borderRadius: '4px',
                  fontSize: '0.9rem',
                  textAlign: 'left'
                }}>
                  <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: '0.3rem', fontSize: '0.8rem', color: 'var(--text-light)' }}>
                    <span style={{ fontWeight: 'bold' }}>{alert.deviceId}</span>
                    <span>{alert.time}</span>
                  </div>
                  <div style={{ color: 'var(--text-color)' }}>{alert.message}</div>
                </div>
              ))
            )}
          </div>
        </div>
      </div>

    </div>
  );
};

export default HomePage;
