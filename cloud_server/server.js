const express = require('express');
const mqtt = require('mqtt');
const bodyParser = require('body-parser');
const cors = require('cors');
const path = require('path');
const { MongoClient } = require('mongodb');

const app = express();
const PORT = process.env.PORT || 3000;

// 中间件配置
app.use(cors());
app.use(bodyParser.json({ limit: '50mb' }));
app.use(bodyParser.urlencoded({ extended: true, limit: '50mb' }));
app.use(express.static(path.join(__dirname, 'public')));

// MongoDB 配置
const MONGODB_URI = process.env.MONGODB_URI || 'mongodb://8.135.238.216:27017';
const MONGODB_DB = process.env.MONGODB_DB || 'esp32_epd';
let db = null;
let devicesCollection = null;
let deviceStatusCollection = null;

// MQTT 配置
const MQTT_BROKER = process.env.MQTT_BROKER || 'mqtt://localhost:1883';
const MQTT_USER = process.env.MQTT_USER || 'admin';
const MQTT_PASS = process.env.MQTT_PASS || 'admin';

// 连接 MQTT Broker
const mqttClient = mqtt.connect(MQTT_BROKER, {
    username: MQTT_USER,
    password: MQTT_PASS,
    clientId: 'cloud-server-' + Math.random().toString(16).substr(2, 8),
    clean: true,
    reconnectPeriod: 1000,
});

// 存储在线设备列表（内存缓存，用于快速查询）
const onlineDevices = new Map();

// ==================== MongoDB 连接 ====================
async function connectMongoDB() {
    try {
        const client = new MongoClient(MONGODB_URI);
        await client.connect();
        db = client.db(MONGODB_DB);
        devicesCollection = db.collection('devices');
        deviceStatusCollection = db.collection('device_status');
        
        // 创建索引
        await devicesCollection.createIndex({ deviceId: 1 }, { unique: true });
        await deviceStatusCollection.createIndex({ deviceId: 1 }, { unique: true });
        await deviceStatusCollection.createIndex({ lastSeen: 1 });
        
        console.log('✅ Connected to MongoDB:', MONGODB_URI);
        console.log('📊 Database:', MONGODB_DB);
    } catch (error) {
        console.error('❌ MongoDB connection error:', error);
        // 即使 MongoDB 连接失败，服务器仍然可以运行（使用内存存储）
        console.log('⚠️  Server will continue with in-memory storage');
    }
}

// ==================== MQTT 连接事件 ====================
mqttClient.on('connect', () => {
    console.log('✅ Connected to MQTT broker:', MQTT_BROKER);
    
    // 订阅所有设备的上行消息
    mqttClient.subscribe('dev/+/up/#', (err) => {
        if (err) {
            console.error('❌ Subscribe error:', err);
        } else {
            console.log('✅ Subscribed to: dev/+/up/#');
        }
    });
});

mqttClient.on('error', (err) => {
    console.error('❌ MQTT error:', err);
});

mqttClient.on('message', async (topic, message) => {
    console.log('📥 MQTT message received:', topic);
    try {
        const msg = JSON.parse(message.toString());
        console.log('   Data:', msg);
        
        // 更新设备状态
        const match = topic.match(/dev\/([^\/]+)\/up\//);
        if (match) {
            const deviceId = match[1];
            const statusData = {
                ...msg,
                lastSeen: Date.now()
            };
            
            // 更新内存缓存
            onlineDevices.set(deviceId, statusData);
            
            // 更新数据库
            if (deviceStatusCollection) {
                try {
                    await deviceStatusCollection.updateOne(
                        { deviceId: deviceId },
                        { 
                            $set: {
                                ...statusData,
                                updatedAt: new Date()
                            }
                        },
                        { upsert: true }
                    );
                } catch (error) {
                    console.error('❌ Failed to update device status in DB:', error);
                }
            }
        }
    } catch (e) {
        console.log('   Raw:', message.toString());
    }
});

// ==================== API: 设备管理 ====================

// 获取所有设备列表（从数据库）
app.get('/api/devices/list', async (req, res) => {
    try {
        if (!devicesCollection) {
            return res.json({ success: true, devices: [] });
        }
        
        const devices = await devicesCollection.find({}).sort({ addedAt: -1 }).toArray();
        res.json({ success: true, devices });
    } catch (error) {
        console.error('❌ Error fetching devices:', error);
        res.status(500).json({ success: false, error: error.message });
    }
});

// 添加设备
app.post('/api/devices/add', async (req, res) => {
    try {
        const { deviceId, deviceName } = req.body;
        
        if (!deviceId) {
            return res.status(400).json({ success: false, error: 'Missing deviceId' });
        }
        
        // 验证设备ID格式（6位或12位十六进制）
        const cleanId = deviceId.trim().toUpperCase().replace(/[-:]/g, '');
        if (!/^[0-9A-F]{6}$|^[0-9A-F]{12}$/.test(cleanId)) {
            return res.status(400).json({ success: false, error: 'Invalid deviceId format' });
        }
        
        if (!devicesCollection) {
            return res.status(500).json({ success: false, error: 'Database not connected' });
        }
        
        // 检查是否已存在
        const existing = await devicesCollection.findOne({ deviceId: cleanId });
        if (existing) {
            return res.status(400).json({ success: false, error: 'Device already exists' });
        }
        
        // 添加设备
        const device = {
            deviceId: cleanId,
            deviceName: deviceName || cleanId,
            addedAt: new Date(),
            createdAt: new Date(),
            updatedAt: new Date()
        };
        
        await devicesCollection.insertOne(device);
        
        console.log('✅ Device added:', cleanId);
        res.json({ success: true, device });
    } catch (error) {
        console.error('❌ Error adding device:', error);
        res.status(500).json({ success: false, error: error.message });
    }
});

// 删除设备
app.delete('/api/devices/:deviceId', async (req, res) => {
    try {
        const { deviceId } = req.params;
        
        if (!devicesCollection) {
            return res.status(500).json({ success: false, error: 'Database not connected' });
        }
        
        const result = await devicesCollection.deleteOne({ deviceId: deviceId });
        
        if (result.deletedCount === 0) {
            return res.status(404).json({ success: false, error: 'Device not found' });
        }
        
        // 同时删除设备状态
        if (deviceStatusCollection) {
            await deviceStatusCollection.deleteOne({ deviceId: deviceId });
        }
        
        // 从内存缓存中删除
        onlineDevices.delete(deviceId);
        
        console.log('✅ Device deleted:', deviceId);
        res.json({ success: true, message: 'Device deleted' });
    } catch (error) {
        console.error('❌ Error deleting device:', error);
        res.status(500).json({ success: false, error: error.message });
    }
});

// 获取设备状态（在线设备 + 数据库中的设备）
app.get('/api/devices', async (req, res) => {
    try {
        // 从数据库获取所有已注册的设备
        let registeredDevices = [];
        if (devicesCollection) {
            registeredDevices = await devicesCollection.find({}).toArray();
        }
        
        // 合并在线状态
        const devices = registeredDevices.map(device => {
            const status = onlineDevices.get(device.deviceId) || null;
            const isOnline = status && (Date.now() - status.lastSeen < 60000);
            
            return {
                deviceId: device.deviceId,
                deviceName: device.deviceName,
                addedAt: device.addedAt,
                online: isOnline,
                ...(status || {})
            };
        });
        
        res.json({ success: true, devices });
    } catch (error) {
        console.error('❌ Error fetching device status:', error);
        res.status(500).json({ success: false, error: error.message });
    }
});

// ==================== API: EPD 控制 ====================

// API: 发送图片数据到指定设备
app.post('/api/epd/init', (req, res) => {
    const { deviceId, epdType } = req.body;
    
    if (!deviceId || !epdType) {
        return res.status(400).json({ success: false, error: 'Missing deviceId or epdType' });
    }

    const topic = `dev/${deviceId}/down/epd`;
    const payload = JSON.stringify({
        cmd: 'EPD',
        type: epdType,
        timestamp: Date.now()
    });

    mqttClient.publish(topic, payload, { qos: 1 }, (err) => {
        if (err) {
            console.error('❌ Publish error:', err);
            res.status(500).json({ success: false, error: err.message });
        } else {
            console.log('✅ EPD init sent to', deviceId);
            res.json({ success: true, message: 'EPD init command sent' });
        }
    });
});

// API: 加载图片数据
app.post('/api/epd/load', (req, res) => {
    const { deviceId, data, length } = req.body;
    
    if (!deviceId || !data) {
        return res.status(400).json({ success: false, error: 'Missing deviceId or data' });
    }

    const topic = `dev/${deviceId}/down/epd`;
    const payload = JSON.stringify({
        cmd: 'LOAD',
        data: data,
        length: length || data.length,
        timestamp: Date.now()
    });

    mqttClient.publish(topic, payload, { qos: 1 }, (err) => {
        if (err) {
            console.error('❌ Publish error:', err);
            res.status(500).json({ success: false, error: err.message });
        } else {
            console.log('✅ Data chunk sent to', deviceId, '- size:', data.length);
            res.json({ success: true, message: 'Data sent' });
        }
    });
});

// API: 切换数据通道
app.post('/api/epd/next', (req, res) => {
    const { deviceId } = req.body;
    
    if (!deviceId) {
        return res.status(400).json({ success: false, error: 'Missing deviceId' });
    }

    const topic = `dev/${deviceId}/down/epd`;
    const payload = JSON.stringify({
        cmd: 'NEXT',
        timestamp: Date.now()
    });

    mqttClient.publish(topic, payload, { qos: 1 }, (err) => {
        if (err) {
            console.error('❌ Publish error:', err);
            res.status(500).json({ success: false, error: err.message });
        } else {
            console.log('✅ NEXT command sent to', deviceId);
            res.json({ success: true, message: 'NEXT command sent' });
        }
    });
});

// API: 显示设备码
app.post('/api/epd/show-device-code', (req, res) => {
    const { deviceId } = req.body;
    
    if (!deviceId) {
        return res.status(400).json({ success: false, error: 'Missing deviceId' });
    }

    const topic = `dev/${deviceId}/down/epd`;
    const payload = JSON.stringify({
        cmd: 'SHOW_DEVICE_CODE',
        timestamp: Date.now()
    });

    mqttClient.publish(topic, payload, { qos: 1 }, (err) => {
        if (err) {
            console.error('❌ Publish error:', err);
            res.status(500).json({ success: false, error: err.message });
        } else {
            console.log('✅ SHOW_DEVICE_CODE command sent to', deviceId);
            res.json({ success: true, message: 'Show device code command sent' });
        }
    });
});

// API: 显示图片
app.post('/api/epd/show', (req, res) => {
    const { deviceId } = req.body;
    
    if (!deviceId) {
        return res.status(400).json({ success: false, error: 'Missing deviceId' });
    }

    const topic = `dev/${deviceId}/down/epd`;
    const payload = JSON.stringify({
        cmd: 'SHOW',
        timestamp: Date.now()
    });

    mqttClient.publish(topic, payload, { qos: 1 }, (err) => {
        if (err) {
            console.error('❌ Publish error:', err);
            res.status(500).json({ success: false, error: err.message });
        } else {
            console.log('✅ SHOW command sent to', deviceId);
            res.json({ success: true, message: 'SHOW command sent' });
        }
    });
});

// ==================== 启动服务器 ====================
async function startServer() {
    // 先连接 MongoDB
    await connectMongoDB();
    
    // 启动 HTTP 服务器
    app.listen(PORT, () => {
        console.log(`\n🚀 Server is running on port ${PORT}`);
        console.log(`📡 Web interface: http://localhost:${PORT}`);
        console.log(`🔌 MQTT broker: ${MQTT_BROKER}`);
        console.log(`💾 MongoDB: ${MONGODB_URI}/${MONGODB_DB}\n`);
    });
}

startServer().catch(console.error);

// 优雅关闭
process.on('SIGINT', () => {
    console.log('\n⏸️  Closing connections...');
    mqttClient.end();
    process.exit();
});
