#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 E-Paper Cloud Server - Flask Backend
"""

import os
import json
import time
import threading
import hashlib
import secrets
from datetime import datetime
from functools import wraps

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
users_collection = None
devices_collection = None
device_status_collection = None
pages_collection = None
page_lists_collection = None

def connect_mongodb():
    """连接 MongoDB"""
    global mongo_client, db, users_collection, devices_collection, device_status_collection
    global pages_collection, page_lists_collection
    try:
        mongo_client = MongoClient(Config.MONGODB_URI, serverSelectionTimeoutMS=5000)
        # 测试连接
        mongo_client.server_info()
        db = mongo_client[Config.MONGODB_DB]
        users_collection = db['users']
        devices_collection = db['devices']
        device_status_collection = db['device_status']
        pages_collection = db['pages']
        page_lists_collection = db['page_lists']
        
        # 创建索引
        users_collection.create_index('username', unique=True)
        users_collection.create_index('token', unique=True, sparse=True)

        devices_collection.create_index('deviceId', unique=True)
        devices_collection.create_index('owner')

        device_status_collection.create_index('deviceId', unique=True)
        device_status_collection.create_index('lastSeen')

        pages_collection.create_index('deviceId')
        pages_collection.create_index([('deviceId', 1), ('name', 1)])

        page_lists_collection.create_index('deviceId')
        page_lists_collection.create_index([('deviceId', 1), ('isActive', 1)])
        
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

# ==================== 用户认证工具函数 ====================

def hash_password(password: str) -> str:
    return hashlib.sha256(password.encode('utf-8')).hexdigest()

def generate_token() -> str:
    return secrets.token_hex(32)

def get_current_user():
    """根据 Authorization: Bearer <token> 获取当前用户"""
    global users_collection
    if users_collection is None:
        return None

    auth_header = request.headers.get('Authorization', '')
    if not auth_header.startswith('Bearer '):
        return None
    token = auth_header[7:].strip()
    if not token:
        return None

    user = users_collection.find_one({'token': token})
    return user

def login_required(f):
    """需要登录的装饰器"""
    @wraps(f)
    def wrapper(*args, **kwargs):
        user = get_current_user()
        if not user:
            return jsonify({'success': False, 'error': 'Unauthorized'}), 401
        # 将用户对象挂到 request 上，后续处理使用
        request.user = user
        return f(*args, **kwargs)
    return wrapper

def ensure_device_owner(device_id: str, user) -> bool:
    """检查设备是否属于当前用户"""
    if devices_collection is None or not user:
        return False
    owner = user.get('username')
    if not owner:
        return False
    device = devices_collection.find_one({'deviceId': device_id, 'owner': owner})
    return device is not None

# ==================== API: 用户注册 / 登录 ====================

@app.route('/api/auth/register', methods=['POST'])
def register():
    """用户注册"""
    global users_collection
    if users_collection is None:
        return jsonify({'success': False, 'error': 'Database not connected'}), 500

    data = request.get_json() or {}
    username = (data.get('username') or '').strip()
    password = (data.get('password') or '').strip()

    if not username or not password:
        return jsonify({'success': False, 'error': '用户名和密码不能为空'}), 400

    if len(username) < 3 or len(password) < 4:
        return jsonify({'success': False, 'error': '用户名或密码太短'}), 400

    try:
        users_collection.insert_one({
            'username': username,
            'passwordHash': hash_password(password),
            'createdAt': datetime.utcnow()
        })
        return jsonify({'success': True, 'message': '注册成功'})
    except DuplicateKeyError:
        return jsonify({'success': False, 'error': '用户名已存在'}), 400
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/auth/login', methods=['POST'])
def login():
    """用户登录，返回 token"""
    global users_collection
    if users_collection is None:
        return jsonify({'success': False, 'error': 'Database not connected'}), 500

    data = request.get_json() or {}
    username = (data.get('username') or '').strip()
    password = (data.get('password') or '').strip()

    if not username or not password:
        return jsonify({'success': False, 'error': '用户名和密码不能为空'}), 400

    user = users_collection.find_one({'username': username})
    if not user or user.get('passwordHash') != hash_password(password):
        return jsonify({'success': False, 'error': '用户名或密码错误'}), 400

    token = generate_token()
    users_collection.update_one(
        {'_id': user['_id']},
        {'$set': {'token': token, 'lastLoginAt': datetime.utcnow()}}
    )

    return jsonify({
        'success': True,
        'token': token,
        'user': {'username': username}
    })

@app.route('/api/auth/logout', methods=['POST'])
@login_required
def logout():
    """退出登录"""
    global users_collection
    user = getattr(request, 'user', None)
    if not user or users_collection is None:
        return jsonify({'success': False, 'error': 'Unauthorized'}), 401

    users_collection.update_one(
        {'_id': user['_id']},
        {'$unset': {'token': ''}}
    )
    return jsonify({'success': True, 'message': 'Logged out'})

@app.route('/api/auth/me', methods=['GET'])
@login_required
def me():
    """获取当前登录用户信息"""
    user = getattr(request, 'user', None)
    return jsonify({
        'success': True,
        'user': {
            'username': user.get('username')
        }
    })

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
@login_required
def get_devices_list():
    """获取当前用户的设备列表"""
    try:
        user = getattr(request, 'user', None)
        if devices_collection is None or not user:
            return jsonify({'success': True, 'devices': []})

        owner = user.get('username')
        devices = list(
            devices_collection.find({'owner': owner}, {'_id': 0})
            .sort('addedAt', -1)
        )
        return jsonify({'success': True, 'devices': devices})
    except Exception as e:
        print(f'❌ Error fetching devices: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/devices/add', methods=['POST'])
@login_required
def add_device():
    """为当前用户添加设备"""
    try:
        user = getattr(request, 'user', None)
        owner = user.get('username') if user else None

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
        
        if devices_collection is None or not owner:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        # 添加设备
        device = {
            'deviceId': clean_id,
            'deviceName': device_name or clean_id,
            'owner': owner,
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
@login_required
def delete_device(device_id):
    """删除当前用户的设备"""
    try:
        user = getattr(request, 'user', None)
        owner = user.get('username') if user else None

        if devices_collection is None or not owner:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        result = devices_collection.delete_one({'deviceId': device_id, 'owner': owner})
        
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
@login_required
def get_devices_status():
    """获取当前用户的设备列表和状态"""
    try:
        user = getattr(request, 'user', None)
        owner = user.get('username') if user else None

        # 从数据库获取所有已注册的设备
        registered_devices = []
        if devices_collection is not None and owner:
            registered_devices = list(
                devices_collection.find({'owner': owner}, {'_id': 0})
            )
        
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

# ==================== API: 页面管理 ====================

@app.route('/api/pages/list/<device_id>', methods=['GET'])
@login_required
def get_pages(device_id):
    """获取设备的所有页面（仅限当前用户的设备）"""
    try:
        user = getattr(request, 'user', None)
        if not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        if pages_collection is None:
            return jsonify({'success': True, 'pages': []})
        
        pages = list(pages_collection.find(
            {'deviceId': device_id}, 
            {'_id': 0}
        ).sort('updatedAt', -1))
        
        # 转换日期
        for page in pages:
            if hasattr(page.get('createdAt'), 'isoformat'):
                page['createdAt'] = page['createdAt'].isoformat()
            if hasattr(page.get('updatedAt'), 'isoformat'):
                page['updatedAt'] = page['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'pages': pages})
    except Exception as e:
        print(f'❌ Error fetching pages: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/pages/save', methods=['POST'])
@login_required
def save_page():
    """保存页面（仅限当前用户的设备）"""
    try:
        data = request.get_json()
        device_id = data.get('deviceId')
        page_id = data.get('pageId')
        page_name = data.get('name', '未命名页面')
        page_type = data.get('type', 'custom')  # custom, image, text, mixed, template
        page_data = data.get('data', {})  # 页面内容数据
        thumbnail = data.get('thumbnail', '')  # 缩略图 base64
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

        user = getattr(request, 'user', None)
        if not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
        
        if pages_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        now = datetime.utcnow()
        
        if page_id:
            # 更新现有页面
            result = pages_collection.update_one(
                {'pageId': page_id, 'deviceId': device_id},
                {'$set': {
                    'name': page_name,
                    'type': page_type,
                    'data': page_data,
                    'thumbnail': thumbnail,
                    'updatedAt': now
                }}
            )
            if result.matched_count == 0:
                return jsonify({'success': False, 'error': 'Page not found'}), 404
            
            print(f'✅ Page updated: {page_id}')
        else:
            # 创建新页面
            import uuid
            page_id = str(uuid.uuid4())[:8]
            
            page = {
                'pageId': page_id,
                'deviceId': device_id,
                'name': page_name,
                'type': page_type,
                'data': page_data,
                'thumbnail': thumbnail,
                'createdAt': now,
                'updatedAt': now
            }
            pages_collection.insert_one(page)
            print(f'✅ Page created: {page_id}')
        
        return jsonify({
            'success': True, 
            'pageId': page_id,
            'message': 'Page saved'
        })
    except Exception as e:
        print(f'❌ Error saving page: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/pages/<page_id>', methods=['GET'])
@login_required
def get_page(page_id):
    """获取单个页面详情（仅限当前用户的设备）"""
    try:
        if pages_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        page = pages_collection.find_one({'pageId': page_id}, {'_id': 0})
        if not page:
            return jsonify({'success': False, 'error': 'Page not found'}), 404

        # 校验设备归属
        user = getattr(request, 'user', None)
        device_id = page.get('deviceId')
        if device_id and not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
        
        # 转换日期
        if hasattr(page.get('createdAt'), 'isoformat'):
            page['createdAt'] = page['createdAt'].isoformat()
        if hasattr(page.get('updatedAt'), 'isoformat'):
            page['updatedAt'] = page['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'page': page})
    except Exception as e:
        print(f'❌ Error fetching page: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/pages/<page_id>', methods=['DELETE'])
@login_required
def delete_page(page_id):
    """删除页面（仅限当前用户的设备）"""
    try:
        if pages_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500

        # 先找到页面，检查归属
        page = pages_collection.find_one({'pageId': page_id})
        if not page:
            return jsonify({'success': False, 'error': 'Page not found'}), 404

        user = getattr(request, 'user', None)
        device_id = page.get('deviceId')
        if device_id and not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        result = pages_collection.delete_one({'pageId': page_id})
        
        # 从所有页面列表中移除该页面
        if page_lists_collection is not None:
            page_lists_collection.update_many(
                {},
                {'$pull': {'pages': {'pageId': page_id}}}
            )
        
        print(f'✅ Page deleted: {page_id}')
        return jsonify({'success': True, 'message': 'Page deleted'})
    except Exception as e:
        print(f'❌ Error deleting page: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

# ==================== API: 页面列表管理 ====================

@app.route('/api/page-lists/list/<device_id>', methods=['GET'])
@login_required
def get_page_lists(device_id):
    """获取设备的所有页面列表（仅限当前用户的设备）"""
    try:
        user = getattr(request, 'user', None)
        if not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        if page_lists_collection is None:
            return jsonify({'success': True, 'pageLists': []})
        
        page_lists = list(page_lists_collection.find(
            {'deviceId': device_id}, 
            {'_id': 0}
        ).sort('updatedAt', -1))
        
        # 转换日期
        for pl in page_lists:
            if hasattr(pl.get('createdAt'), 'isoformat'):
                pl['createdAt'] = pl['createdAt'].isoformat()
            if hasattr(pl.get('updatedAt'), 'isoformat'):
                pl['updatedAt'] = pl['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'pageLists': page_lists})
    except Exception as e:
        print(f'❌ Error fetching page lists: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/page-lists/save', methods=['POST'])
@login_required
def save_page_list():
    """保存页面列表（仅限当前用户的设备）"""
    try:
        data = request.get_json()
        device_id = data.get('deviceId')
        list_id = data.get('listId')
        list_name = data.get('name', '默认页面列表')
        pages = data.get('pages', [])  # [{pageId, order}]
        interval = data.get('interval', 60)  # 切换间隔(分钟)
        is_active = data.get('isActive', False)
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

        user = getattr(request, 'user', None)
        if not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
        
        if page_lists_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        now = datetime.utcnow()
        
        # 如果设置为激活，先取消其他列表的激活状态
        if is_active:
            page_lists_collection.update_many(
                {'deviceId': device_id},
                {'$set': {'isActive': False}}
            )
        
        if list_id:
            # 更新现有列表
            result = page_lists_collection.update_one(
                {'listId': list_id, 'deviceId': device_id},
                {'$set': {
                    'name': list_name,
                    'pages': pages,
                    'interval': interval,
                    'isActive': is_active,
                    'updatedAt': now
                }}
            )
            if result.matched_count == 0:
                return jsonify({'success': False, 'error': 'Page list not found'}), 404
            
            print(f'✅ Page list updated: {list_id}')
        else:
            # 创建新列表
            import uuid
            list_id = str(uuid.uuid4())[:8]
            
            page_list = {
                'listId': list_id,
                'deviceId': device_id,
                'name': list_name,
                'pages': pages,
                'interval': interval,
                'isActive': is_active,
                'createdAt': now,
                'updatedAt': now
            }
            page_lists_collection.insert_one(page_list)
            print(f'✅ Page list created: {list_id}')
        
        return jsonify({
            'success': True, 
            'listId': list_id,
            'message': 'Page list saved'
        })
    except Exception as e:
        print(f'❌ Error saving page list: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/page-lists/<list_id>', methods=['DELETE'])
@login_required
def delete_page_list(list_id):
    """删除页面列表（仅限当前用户的设备）"""
    try:
        if page_lists_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500

        # 找到列表，检查归属
        page_list = page_lists_collection.find_one({'listId': list_id})
        if not page_list:
            return jsonify({'success': False, 'error': 'Page list not found'}), 404

        user = getattr(request, 'user', None)
        device_id = page_list.get('deviceId')
        if device_id and not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        result = page_lists_collection.delete_one({'listId': list_id})
        
        print(f'✅ Page list deleted: {list_id}')
        return jsonify({'success': True, 'message': 'Page list deleted'})
    except Exception as e:
        print(f'❌ Error deleting page list: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/page-lists/active/<device_id>', methods=['GET'])
@login_required
def get_active_page_list(device_id):
    """获取设备当前激活的页面列表（仅限当前用户的设备）"""
    try:
        user = getattr(request, 'user', None)
        if not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        if page_lists_collection is None:
            return jsonify({'success': True, 'pageList': None})
        
        page_list = page_lists_collection.find_one(
            {'deviceId': device_id, 'isActive': True},
            {'_id': 0}
        )
        
        if page_list:
            if hasattr(page_list.get('createdAt'), 'isoformat'):
                page_list['createdAt'] = page_list['createdAt'].isoformat()
            if hasattr(page_list.get('updatedAt'), 'isoformat'):
                page_list['updatedAt'] = page_list['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'pageList': page_list})
    except Exception as e:
        print(f'❌ Error fetching active page list: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

# ==================== API: 模板 ====================

TEMPLATES = [
    {
        'templateId': 'clock',
        'name': '时钟',
        'icon': '🕐',
        'description': '显示当前时间和日期',
        'preview': '/templates/clock.png',
        'defaultData': {
            'type': 'template',
            'template': 'clock',
            'showDate': True,
            'showWeekday': True,
            'format24h': True
        }
    },
    {
        'templateId': 'weather',
        'name': '天气',
        'icon': '🌤️',
        'description': '显示天气信息',
        'preview': '/templates/weather.png',
        'defaultData': {
            'type': 'template',
            'template': 'weather',
            'city': '',
            'showForecast': True
        }
    },
    {
        'templateId': 'calendar',
        'name': '日历',
        'icon': '📅',
        'description': '显示日历和日程',
        'preview': '/templates/calendar.png',
        'defaultData': {
            'type': 'template',
            'template': 'calendar',
            'showEvents': True
        }
    },
    {
        'templateId': 'todo',
        'name': '待办事项',
        'icon': '✅',
        'description': '显示待办事项列表',
        'preview': '/templates/todo.png',
        'defaultData': {
            'type': 'template',
            'template': 'todo',
            'items': []
        }
    },
    {
        'templateId': 'quote',
        'name': '每日一言',
        'icon': '💬',
        'description': '显示励志名言或诗词',
        'preview': '/templates/quote.png',
        'defaultData': {
            'type': 'template',
            'template': 'quote',
            'category': 'motivational'
        }
    },
    {
        'templateId': 'counter',
        'name': '计数器',
        'icon': '🔢',
        'description': '显示倒计时或正计时',
        'preview': '/templates/counter.png',
        'defaultData': {
            'type': 'template',
            'template': 'counter',
            'targetDate': '',
            'title': '距离目标'
        }
    },
    {
        'templateId': 'qrcode',
        'name': '二维码',
        'icon': '📱',
        'description': '显示自定义二维码',
        'preview': '/templates/qrcode.png',
        'defaultData': {
            'type': 'template',
            'template': 'qrcode',
            'content': '',
            'title': ''
        }
    },
    {
        'templateId': 'blank',
        'name': '空白画布',
        'icon': '⬜',
        'description': '从空白开始自由创作',
        'preview': '/templates/blank.png',
        'defaultData': {
            'type': 'custom',
            'elements': []
        }
    }
]

@app.route('/api/templates', methods=['GET'])
def get_templates():
    """获取所有可用模板"""
    return jsonify({'success': True, 'templates': TEMPLATES})

@app.route('/api/templates/<template_id>', methods=['GET'])
def get_template(template_id):
    """获取单个模板详情"""
    template = next((t for t in TEMPLATES if t['templateId'] == template_id), None)
    if not template:
        return jsonify({'success': False, 'error': 'Template not found'}), 404
    return jsonify({'success': True, 'template': template})

# ==================== API: EPD 控制 ====================

@app.route('/api/epd/init', methods=['POST'])
@login_required
def epd_init():
    """初始化 EPD（仅限当前用户的设备）"""
    data = request.get_json()
    device_id = data.get('deviceId')
    epd_type = data.get('epdType')
    
    if not device_id or not epd_type:
        return jsonify({'success': False, 'error': 'Missing deviceId or epdType'}), 400

    user = getattr(request, 'user', None)
    if not ensure_device_owner(device_id, user):
        return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
    
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
@login_required
def epd_load():
    """加载图片数据（仅限当前用户的设备）"""
    data = request.get_json()
    device_id = data.get('deviceId')
    image_data = data.get('data')
    length = data.get('length')
    
    if not device_id or not image_data:
        return jsonify({'success': False, 'error': 'Missing deviceId or data'}), 400

    user = getattr(request, 'user', None)
    if not ensure_device_owner(device_id, user):
        return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
    
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
@login_required
def epd_next():
    """切换数据通道（仅限当前用户的设备）"""
    data = request.get_json()
    device_id = data.get('deviceId')
    
    if not device_id:
        return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

    user = getattr(request, 'user', None)
    if not ensure_device_owner(device_id, user):
        return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
    
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
@login_required
def epd_show_device_code():
    """显示设备码（仅限当前用户的设备）"""
    data = request.get_json()
    device_id = data.get('deviceId')
    
    if not device_id:
        return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

    user = getattr(request, 'user', None)
    if not ensure_device_owner(device_id, user):
        return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
    
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
@login_required
def epd_show():
    """显示图片（仅限当前用户的设备）"""
    data = request.get_json()
    device_id = data.get('deviceId')
    
    if not device_id:
        return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

    user = getattr(request, 'user', None)
    if not ensure_device_owner(device_id, user):
        return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
    
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

# ==================== API: 自研3色算法处理 ====================

@app.route('/api/epd/process-tricolor-custom', methods=['POST'])
@login_required
def process_tricolor_custom():
    """使用自研3色算法处理图片（仅限当前用户的设备）"""
    try:
        from PIL import Image
        import base64
        import io
        from tri_color_epd import process_tricolor_image, build_preview_image, RedMaskParams, BlackPlaneParams
        
        data = request.get_json()
        image_data = data.get('imageData')
        width = data.get('width', 800)
        height = data.get('height', 480)
        
        if not image_data:
            return jsonify({'success': False, 'error': 'Missing imageData'}), 400
        
        # 解码 base64 图片
        try:
            img_bytes = base64.b64decode(image_data)
            img = Image.open(io.BytesIO(img_bytes)).convert('RGB')
        except Exception as e:
            return jsonify({'success': False, 'error': f'Invalid image data: {str(e)}'}), 400
        
        # 调整图片大小
        if img.size != (width, height):
            img = img.resize((width, height), Image.LANCZOS)
        
        # 调用自研3色算法处理
        # 使用更宽松的红色检测参数，更容易检测到红色
        red_params = RedMaskParams(
            h_low=30.0,   # 扩大色相范围（默认20）
            h_high=330.0, # 扩大色相范围（默认340）
            s_min=0.20,   # 降低饱和度要求（默认0.35）
            v_min=0.15,   # 降低亮度要求（默认0.25）
            rg_min=30,    # 降低RGB差值要求（默认50）
            rb_min=30,    # 降低RGB差值要求（默认50）
            open_iters=1, # 形态学开运算次数（去散点）
            close_iters=1, # 形态学闭运算次数（补小洞）
        )
        
        result = process_tricolor_image(
            img,
            target_size=(width, height),
            red_params=red_params,
            black_params=BlackPlaneParams(
                gamma=1.0,
                threshold=0.5,
                dither='bayer8',  # 使用 Bayer 8x8 有序抖动
            ),
        )
        
        # 调试信息：统计红色像素数量
        red_pixel_count = int(result.red_plane.sum())
        total_pixels = result.red_plane.size
        red_percentage = (red_pixel_count / total_pixels * 100) if total_pixels > 0 else 0
        print(f'🔴 红色像素统计: {red_pixel_count}/{total_pixels} ({red_percentage:.2f}%)')
        
        # 如果红色像素太少，尝试更宽松的参数
        if red_pixel_count < 10:  # 如果红色像素少于10个
            print('⚠️  红色像素太少，尝试使用更宽松的参数重新处理...')
            red_params_loose = RedMaskParams(
                h_low=40.0,
                h_high=320.0,
                s_min=0.15,
                v_min=0.10,
                rg_min=20,
                rb_min=20,
                open_iters=0,  # 不做形态学处理，保留更多红色
                close_iters=0,
            )
            result = process_tricolor_image(
                img,
                target_size=(width, height),
                red_params=red_params_loose,
                black_params=BlackPlaneParams(
                    gamma=1.0,
                    threshold=0.5,
                    dither='bayer8',
                ),
            )
            red_pixel_count = int(result.red_plane.sum())
            print(f'🔴 宽松参数后红色像素: {red_pixel_count}/{total_pixels} ({red_pixel_count/total_pixels*100:.2f}%)')
        
        # 生成预览图
        preview_img = build_preview_image(result.black_plane, result.red_plane)
        
        # 将预览图转为 base64
        preview_buffer = io.BytesIO()
        preview_img.save(preview_buffer, format='PNG')
        preview_base64 = base64.b64encode(preview_buffer.getvalue()).decode('utf-8')
        
        # 将红色通道数据转为数组（用于前端显示统计）
        red_channel_array = result.red_plane.flatten().astype(int).tolist()
        
        return jsonify({
            'success': True,
            'previewImage': preview_base64,
            'redChannelData': red_channel_array,
            'width': width,
            'height': height
        })
        
    except ImportError as e:
        print(f'❌ Import error: {e}')
        return jsonify({'success': False, 'error': 'Processing module not available'}), 500
    except Exception as e:
        print(f'❌ Processing error: {e}')
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'error': str(e)}), 500

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
