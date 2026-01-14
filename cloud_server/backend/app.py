#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 E-Paper Cloud Server - Flask Backend
"""

import os
import json
import time
import threading
from datetime import datetime

from flask import Flask, request, jsonify
from flask_cors import CORS
from pymongo import MongoClient
from pymongo.errors import DuplicateKeyError
import paho.mqtt.client as mqtt

from config import Config

# ==================== Flask 应用初始化 ====================
app = Flask(__name__)
app.config.from_object(Config)
CORS(app)  # 允许跨域请求

# ==================== MongoDB 连接 ====================
mongo_client = None
db = None
devices_collection = None
device_status_collection = None

def connect_mongodb():
    """连接 MongoDB"""
    global mongo_client, db, devices_collection, device_status_collection
    try:
        mongo_client = MongoClient(Config.MONGODB_URI, serverSelectionTimeoutMS=5000)
        # 测试连接
        mongo_client.server_info()
        db = mongo_client[Config.MONGODB_DB]
        devices_collection = db['devices']
        device_status_collection = db['device_status']
        
        # 创建索引
        devices_collection.create_index('deviceId', unique=True)
        device_status_collection.create_index('deviceId', unique=True)
        device_status_collection.create_index('lastSeen')
        
        print(f'✅ Connected to MongoDB: {Config.MONGODB_URI}')
        print(f'📊 Database: {Config.MONGODB_DB}')
        return True
    except Exception as e:
        print(f'❌ MongoDB connection error: {e}')
        print('⚠️  Server will continue with in-memory storage')
        return False

# ==================== MQTT 连接 ====================
mqtt_client = None
online_devices = {}  # 内存缓存

def on_mqtt_connect(client, userdata, flags, rc):
    """MQTT 连接回调"""
    if rc == 0:
        print(f'✅ Connected to MQTT broker: {Config.MQTT_BROKER}:{Config.MQTT_PORT}')
        # 订阅所有设备的上行消息
        client.subscribe('dev/+/up/#')
        print('✅ Subscribed to: dev/+/up/#')
    else:
        print(f'❌ MQTT connection failed with code {rc}')

def on_mqtt_message(client, userdata, msg):
    """MQTT 消息回调"""
    topic = msg.topic
    print(f'📥 MQTT message received: {topic}')
    
    try:
        payload = json.loads(msg.payload.decode('utf-8'))
        print(f'   Data: {payload}')
        
        # 解析设备ID
        parts = topic.split('/')
        if len(parts) >= 2 and parts[0] == 'dev':
            device_id = parts[1]
            status_data = {
                **payload,
                'lastSeen': int(time.time() * 1000)
            }
            
            # 更新内存缓存
            online_devices[device_id] = status_data
            
            # 更新数据库
            if device_status_collection is not None:
                try:
                    device_status_collection.update_one(
                        {'deviceId': device_id},
                        {'$set': {
                            **status_data,
                            'updatedAt': datetime.utcnow()
                        }},
                        upsert=True
                    )
                except Exception as e:
                    print(f'❌ Failed to update device status in DB: {e}')
    except json.JSONDecodeError:
        print(f'   Raw: {msg.payload.decode("utf-8")}')
    except Exception as e:
        print(f'❌ Error processing message: {e}')

def connect_mqtt():
    """连接 MQTT Broker"""
    global mqtt_client
    try:
        mqtt_client = mqtt.Client(
            client_id=f'cloud-server-{os.urandom(4).hex()}'
        )
        mqtt_client.username_pw_set(Config.MQTT_USER, Config.MQTT_PASS)
        mqtt_client.on_connect = on_mqtt_connect
        mqtt_client.on_message = on_mqtt_message
        
        mqtt_client.connect(Config.MQTT_BROKER, Config.MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()  # 启动后台线程处理 MQTT
        return True
    except Exception as e:
        print(f'❌ MQTT connection error: {e}')
        return False

def publish_mqtt(topic, payload):
    """发布 MQTT 消息"""
    if mqtt_client is None:
        return False, 'MQTT not connected'
    
    try:
        payload_str = json.dumps(payload)
        result = mqtt_client.publish(topic, payload_str, qos=1)
        if result.rc == 0:
            return True, None
        else:
            return False, f'Publish failed with code {result.rc}'
    except Exception as e:
        return False, str(e)

# ==================== API: 设备管理 ====================

@app.route('/api/devices/list', methods=['GET'])
def get_devices_list():
    """获取所有已注册设备列表"""
    try:
        if devices_collection is None:
            return jsonify({'success': True, 'devices': []})
        
        devices = list(devices_collection.find({}, {'_id': 0}).sort('addedAt', -1))
        return jsonify({'success': True, 'devices': devices})
    except Exception as e:
        print(f'❌ Error fetching devices: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/devices/add', methods=['POST'])
def add_device():
    """添加设备"""
    try:
        data = request.get_json()
        device_id = data.get('deviceId', '').strip().upper()
        device_name = data.get('deviceName', '').strip()
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400
        
        # 清理设备ID（去掉分隔符）
        clean_id = device_id.replace('-', '').replace(':', '')
        
        # 验证格式（6位或12位十六进制）
        import re
        if not re.match(r'^[0-9A-F]{6}$|^[0-9A-F]{12}$', clean_id):
            return jsonify({'success': False, 'error': 'Invalid deviceId format'}), 400
        
        if devices_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        # 添加设备
        device = {
            'deviceId': clean_id,
            'deviceName': device_name or clean_id,
            'addedAt': datetime.utcnow(),
            'createdAt': datetime.utcnow(),
            'updatedAt': datetime.utcnow()
        }
        
        try:
            devices_collection.insert_one(device)
        except DuplicateKeyError:
            return jsonify({'success': False, 'error': 'Device already exists'}), 400
        
        print(f'✅ Device added: {clean_id}')
        # 返回时去掉 _id
        device.pop('_id', None)
        # 转换日期为字符串
        device['addedAt'] = device['addedAt'].isoformat()
        device['createdAt'] = device['createdAt'].isoformat()
        device['updatedAt'] = device['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'device': device})
    except Exception as e:
        print(f'❌ Error adding device: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/devices/<device_id>', methods=['DELETE'])
def delete_device(device_id):
    """删除设备"""
    try:
        if devices_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        result = devices_collection.delete_one({'deviceId': device_id})
        
        if result.deleted_count == 0:
            return jsonify({'success': False, 'error': 'Device not found'}), 404
        
        # 同时删除设备状态
        if device_status_collection is not None:
            device_status_collection.delete_one({'deviceId': device_id})
        
        # 从内存缓存中删除
        online_devices.pop(device_id, None)
        
        print(f'✅ Device deleted: {device_id}')
        return jsonify({'success': True, 'message': 'Device deleted'})
    except Exception as e:
        print(f'❌ Error deleting device: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/devices', methods=['GET'])
def get_devices_status():
    """获取设备列表和状态"""
    try:
        # 从数据库获取所有已注册的设备
        registered_devices = []
        if devices_collection is not None:
            registered_devices = list(devices_collection.find({}, {'_id': 0}))
        
        # 合并在线状态
        current_time = int(time.time() * 1000)
        devices = []
        for device in registered_devices:
            device_id = device['deviceId']
            status = online_devices.get(device_id)
            is_online = status and (current_time - status.get('lastSeen', 0) < 60000)
            
            device_info = {
                'deviceId': device_id,
                'deviceName': device.get('deviceName', device_id),
                'addedAt': device.get('addedAt').isoformat() if hasattr(device.get('addedAt'), 'isoformat') else device.get('addedAt'),
                'online': is_online
            }
            
            if status:
                device_info.update(status)
            
            devices.append(device_info)
        
        return jsonify({'success': True, 'devices': devices})
    except Exception as e:
        print(f'❌ Error fetching device status: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

# ==================== API: EPD 控制 ====================

@app.route('/api/epd/init', methods=['POST'])
def epd_init():
    """初始化 EPD"""
    data = request.get_json()
    device_id = data.get('deviceId')
    epd_type = data.get('epdType')
    
    if not device_id or not epd_type:
        return jsonify({'success': False, 'error': 'Missing deviceId or epdType'}), 400
    
    topic = f'dev/{device_id}/down/epd'
    payload = {
        'cmd': 'EPD',
        'type': epd_type,
        'timestamp': int(time.time() * 1000)
    }
    
    success, error = publish_mqtt(topic, payload)
    if success:
        print(f'✅ EPD init sent to {device_id}')
        return jsonify({'success': True, 'message': 'EPD init command sent'})
    else:
        print(f'❌ Publish error: {error}')
        return jsonify({'success': False, 'error': error}), 500

@app.route('/api/epd/load', methods=['POST'])
def epd_load():
    """加载图片数据"""
    data = request.get_json()
    device_id = data.get('deviceId')
    image_data = data.get('data')
    length = data.get('length')
    
    if not device_id or not image_data:
        return jsonify({'success': False, 'error': 'Missing deviceId or data'}), 400
    
    topic = f'dev/{device_id}/down/epd'
    payload = {
        'cmd': 'LOAD',
        'data': image_data,
        'length': length or len(image_data),
        'timestamp': int(time.time() * 1000)
    }
    
    success, error = publish_mqtt(topic, payload)
    if success:
        print(f'✅ Data chunk sent to {device_id} - size: {len(image_data)}')
        return jsonify({'success': True, 'message': 'Data sent'})
    else:
        print(f'❌ Publish error: {error}')
        return jsonify({'success': False, 'error': error}), 500

@app.route('/api/epd/next', methods=['POST'])
def epd_next():
    """切换数据通道"""
    data = request.get_json()
    device_id = data.get('deviceId')
    
    if not device_id:
        return jsonify({'success': False, 'error': 'Missing deviceId'}), 400
    
    topic = f'dev/{device_id}/down/epd'
    payload = {
        'cmd': 'NEXT',
        'timestamp': int(time.time() * 1000)
    }
    
    success, error = publish_mqtt(topic, payload)
    if success:
        print(f'✅ NEXT command sent to {device_id}')
        return jsonify({'success': True, 'message': 'NEXT command sent'})
    else:
        print(f'❌ Publish error: {error}')
        return jsonify({'success': False, 'error': error}), 500

@app.route('/api/epd/show-device-code', methods=['POST'])
def epd_show_device_code():
    """显示设备码"""
    data = request.get_json()
    device_id = data.get('deviceId')
    
    if not device_id:
        return jsonify({'success': False, 'error': 'Missing deviceId'}), 400
    
    topic = f'dev/{device_id}/down/epd'
    payload = {
        'cmd': 'SHOW_DEVICE_CODE',
        'timestamp': int(time.time() * 1000)
    }
    
    success, error = publish_mqtt(topic, payload)
    if success:
        print(f'✅ SHOW_DEVICE_CODE command sent to {device_id}')
        return jsonify({'success': True, 'message': 'Show device code command sent'})
    else:
        print(f'❌ Publish error: {error}')
        return jsonify({'success': False, 'error': error}), 500

@app.route('/api/epd/show', methods=['POST'])
def epd_show():
    """显示图片"""
    data = request.get_json()
    device_id = data.get('deviceId')
    
    if not device_id:
        return jsonify({'success': False, 'error': 'Missing deviceId'}), 400
    
    topic = f'dev/{device_id}/down/epd'
    payload = {
        'cmd': 'SHOW',
        'timestamp': int(time.time() * 1000)
    }
    
    success, error = publish_mqtt(topic, payload)
    if success:
        print(f'✅ SHOW command sent to {device_id}')
        return jsonify({'success': True, 'message': 'SHOW command sent'})
    else:
        print(f'❌ Publish error: {error}')
        return jsonify({'success': False, 'error': error}), 500

# ==================== 健康检查 ====================

@app.route('/api/health', methods=['GET'])
def health_check():
    """健康检查"""
    mongo_ok = mongo_client is not None
    mqtt_ok = mqtt_client is not None and mqtt_client.is_connected()
    
    return jsonify({
        'success': True,
        'status': 'healthy' if (mongo_ok and mqtt_ok) else 'degraded',
        'mongodb': 'connected' if mongo_ok else 'disconnected',
        'mqtt': 'connected' if mqtt_ok else 'disconnected'
    })

# ==================== 启动服务器 ====================

def init_app():
    """初始化应用"""
    print('\n🚀 Starting ESP32 E-Paper Cloud Server...')
    print(f'📡 MQTT Broker: {Config.MQTT_BROKER}:{Config.MQTT_PORT}')
    print(f'💾 MongoDB: {Config.MONGODB_URI}/{Config.MONGODB_DB}\n')
    
    connect_mongodb()
    connect_mqtt()

# 初始化
init_app()

if __name__ == '__main__':
    port = int(os.environ.get('PORT', 5000))
    print(f'\n🌐 API Server running on http://0.0.0.0:{port}\n')
    app.run(host='0.0.0.0', port=port, debug=Config.DEBUG)
