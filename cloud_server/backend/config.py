import os

class Config:
    """Flask 配置"""
    
    # Flask
    SECRET_KEY = os.environ.get('SECRET_KEY', 'esp32-epd-secret-key')
    DEBUG = os.environ.get('FLASK_DEBUG', 'false').lower() == 'true'
    
    # MongoDB
    MONGODB_URI = os.environ.get('MONGODB_URI', 'mongodb://mongo_miXkxe:mongo_NQf5WW@8.135.238.216:27017/esp32_epd?authSource=admin')
    MONGODB_DB = os.environ.get('MONGODB_DB', 'esp32_epd')
    
    # MQTT
    MQTT_BROKER = os.environ.get('MQTT_BROKER', '8.135.238.216')
    MQTT_PORT = int(os.environ.get('MQTT_PORT', 1883))
    MQTT_USER = os.environ.get('MQTT_USER', 'admin')
    MQTT_PASS = os.environ.get('MQTT_PASS', 'admin')
    
    # Flask (用于构建下载URL)
    FLASK_HOST = os.environ.get('FLASK_HOST', MQTT_BROKER)  # 默认与MQTT broker相同
    FLASK_PORT = int(os.environ.get('FLASK_PORT', 5000))
