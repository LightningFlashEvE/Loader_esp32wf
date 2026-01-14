#!/bin/bash

# ESP32 E-Paper Cloud Server 一键安装脚本
# 适用于 Ubuntu 20.04+

set -e

echo "========================================="
echo "  ESP32 E-Paper Cloud Server 安装"
echo "========================================="
echo ""

# 检查是否为root用户
if [ "$EUID" -ne 0 ]; then 
    echo "❌ 请使用root用户运行此脚本"
    echo "   使用命令: sudo bash install.sh"
    exit 1
fi

# 1. 更新系统
echo "📦 更新系统包..."
apt update
apt upgrade -y

# 2. 安装Mosquitto
echo ""
echo "🦟 安装MQTT Broker (Mosquitto)..."
apt install -y mosquitto mosquitto-clients

# 配置Mosquitto
echo "📝 配置Mosquitto..."
mosquitto_passwd -b -c /etc/mosquitto/passwd admin admin

cat > /etc/mosquitto/mosquitto.conf << 'EOF'
listener 1883 0.0.0.0
allow_anonymous false
password_file /etc/mosquitto/passwd
persistence true
persistence_location /var/lib/mosquitto/
log_dest file /var/log/mosquitto/mosquitto.log
EOF

systemctl restart mosquitto
systemctl enable mosquitto

echo "✅ Mosquitto安装完成"

# 3. 安装Node.js
echo ""
echo "📦 安装Node.js..."
curl -fsSL https://deb.nodesource.com/setup_18.x | bash -
apt install -y nodejs

echo "✅ Node.js $(node --version) 安装完成"

# 4. 安装PM2
echo ""
echo "⚙️  安装PM2..."
npm install -g pm2

# 5. 安装依赖
echo ""
echo "📦 安装项目依赖..."
npm install

# 6. 创建.env文件
echo ""
echo "⚙️  创建配置文件..."
cat > .env << 'EOF'
PORT=3000
MQTT_BROKER=mqtt://localhost:1883
MQTT_USER=admin
MQTT_PASS=admin
EOF

# 7. 配置防火墙
echo ""
echo "🔥 配置防火墙..."
ufw --force enable
ufw allow 22/tcp   # SSH
ufw allow 80/tcp   # HTTP
ufw allow 443/tcp  # HTTPS
ufw allow 3000/tcp # Web服务
ufw allow 1883/tcp # MQTT

echo "✅ 防火墙配置完成"

# 8. 启动服务
echo ""
echo "🚀 启动服务..."
pm2 start server.js --name esp32-cloud
pm2 startup
pm2 save

# 9. 测试连接
echo ""
echo "🧪 测试MQTT连接..."
timeout 2 mosquitto_sub -h localhost -p 1883 -u admin -P admin -t test &
sleep 1
mosquitto_pub -h localhost -p 1883 -u admin -P admin -t test -m "Test message"
sleep 1

# 10. 获取服务器IP
SERVER_IP=$(curl -s ifconfig.me || hostname -I | awk '{print $1}')

# 11. 完成
echo ""
echo "========================================="
echo "  ✅ 安装完成！"
echo "========================================="
echo ""
echo "📋 服务信息："
echo "   - Web界面: http://${SERVER_IP}:3000"
echo "   - MQTT地址: ${SERVER_IP}:1883"
echo "   - MQTT用户: admin"
echo "   - MQTT密码: admin"
echo ""
echo "📝 ESP32配置："
echo "   修改ESP32代码中的以下配置："
echo "   const char* MQTT_HOST = \"${SERVER_IP}\";"
echo "   const char* WIFI_SSID = \"你的WiFi名称\";"
echo "   const char* WIFI_PASSWORD = \"你的WiFi密码\";"
echo ""
echo "🔧 管理命令："
echo "   pm2 status          - 查看服务状态"
echo "   pm2 logs            - 查看日志"
echo "   pm2 restart all     - 重启服务"
echo "   pm2 stop all        - 停止服务"
echo ""
echo "📖 详细文档: README.md"
echo "========================================="
