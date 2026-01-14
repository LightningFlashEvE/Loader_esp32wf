# 快速部署指南

## 方案一：最简单部署（云服务器 + ESP32）

### 步骤1：准备云服务器

你已经有了服务器IP：`8.135.238.216`

SSH连接到服务器：
```bash
ssh root@8.135.238.216
```

### 步骤2：安装MQTT Broker

```bash
# 安装Mosquitto
apt update
apt install -y mosquitto mosquitto-clients

# 创建密码文件
mosquitto_passwd -c /etc/mosquitto/passwd admin
# 输入密码两次: admin

# 配置文件
cat > /etc/mosquitto/mosquitto.conf << 'EOF'
listener 1883 0.0.0.0
allow_anonymous false
password_file /etc/mosquitto/passwd
EOF

# 重启服务
systemctl restart mosquitto
systemctl enable mosquitto

# 测试
mosquitto_sub -h localhost -p 1883 -u admin -P admin -t test &
mosquitto_pub -h localhost -p 1883 -u admin -P admin -t test -m "Hello"
```

### 步骤3：安装Node.js

```bash
# 安装Node.js 18
curl -fsSL https://deb.nodesource.com/setup_18.x | bash -
apt install -y nodejs

# 验证
node --version
npm --version
```

### 步骤4：部署Web服务

```bash
# 创建项目目录
mkdir -p /opt/esp32-cloud
cd /opt/esp32-cloud

# 这里你需要上传cloud_server文件夹的内容
# 可以使用scp或git

# 假设你已经上传了文件，进入目录
cd cloud_server

# 安装依赖
npm install

# 创建环境变量
cat > .env << 'EOF'
PORT=3000
MQTT_BROKER=mqtt://localhost:1883
MQTT_USER=admin
MQTT_PASS=admin
EOF

# 测试运行
node server.js
```

看到输出：
```
🚀 Server is running on port 3000
📡 Web interface: http://localhost:3000
🔌 MQTT broker: mqtt://localhost:1883
```

按 `Ctrl+C` 停止，然后使用PM2管理：

```bash
# 安装PM2
npm install -g pm2

# 启动服务
pm2 start server.js --name esp32-cloud

# 设置开机自启
pm2 startup
pm2 save

# 查看状态
pm2 status
pm2 logs esp32-cloud
```

### 步骤5：配置防火墙

```bash
# 开放端口
ufw allow 3000/tcp
ufw allow 1883/tcp
ufw status
```

### 步骤6：配置ESP32

1. 在Arduino IDE中安装库：
   - `PubSubClient`
   - `ArduinoJson`

2. 修改ESP32代码：

```cpp
// 在 esp32_mqtt_epd.ino 中修改：
const char* WIFI_SSID     = "你的WiFi名";
const char* WIFI_PASSWORD = "你的WiFi密码";
const char* MQTT_HOST = "8.135.238.216";  // 你的服务器IP
```

3. 将原项目的这些文件复制到 `esp32_mqtt_epd/` 文件夹：
   - epd.h
   - buff.h
   - epd13in3.h
   - epd1in54.h
   - epd2in13.h
   - epd2in66.h
   - epd2in7.h
   - epd2in9.h
   - epd3in52.h
   - epd3in7.h
   - epd4in01f.h
   - epd4in2.h
   - epd4in26.h
   - epd5in65f.h
   - epd5in83.h
   - epd7in3.h
   - epd7in5_HD.h
   - epd7in5.h

4. 编译上传到ESP32

5. 打开串口监视器，记录显示的设备ID（例如：`C3-7CDFA1B2C3D4`）

### 步骤7：测试

1. 浏览器访问：`http://8.135.238.216:3000`

2. 在Web界面：
   - 输入ESP32的设备ID
   - 选择墨水屏型号
   - 上传图片
   - 点击"处理图片"
   - 点击"上传到设备"

3. 观察ESP32串口输出和墨水屏显示

---

## 方案二：使用Nginx反向代理（推荐生产环境）

### 安装Nginx

```bash
apt install -y nginx
```

### 配置

```bash
cat > /etc/nginx/sites-available/esp32-cloud << 'EOF'
server {
    listen 80;
    server_name 8.135.238.216;  # 或你的域名

    # Web服务
    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_cache_bypass $http_upgrade;
    }
}
EOF

ln -sf /etc/nginx/sites-available/esp32-cloud /etc/nginx/sites-enabled/
rm -f /etc/nginx/sites-enabled/default
nginx -t
systemctl reload nginx
```

现在可以通过80端口访问：`http://8.135.238.216`

---

## 方案三：使用Docker部署

### Docker Compose配置

创建 `docker-compose.yml`：

```yaml
version: '3.8'

services:
  mosquitto:
    image: eclipse-mosquitto:2
    container_name: mosquitto
    ports:
      - "1883:1883"
      - "9001:9001"
    volumes:
      - ./mosquitto/config:/mosquitto/config
      - ./mosquitto/data:/mosquitto/data
      - ./mosquitto/log:/mosquitto/log
    restart: unless-stopped

  esp32-cloud:
    build: ./cloud_server
    container_name: esp32-cloud
    ports:
      - "3000:3000"
    environment:
      - PORT=3000
      - MQTT_BROKER=mqtt://mosquitto:1883
      - MQTT_USER=admin
      - MQTT_PASS=admin
    depends_on:
      - mosquitto
    restart: unless-stopped
```

创建Mosquitto配置：

```bash
mkdir -p mosquitto/config
cat > mosquitto/config/mosquitto.conf << 'EOF'
listener 1883
allow_anonymous false
password_file /mosquitto/config/passwd
EOF

# 创建密码文件
docker run -it --rm -v $(pwd)/mosquitto/config:/mosquitto/config eclipse-mosquitto:2 mosquitto_passwd -c /mosquitto/config/passwd admin
```

启动：

```bash
docker-compose up -d
docker-compose logs -f
```

---

## 常见问题

### Q1: ESP32连接不上MQTT

**检查清单：**
```bash
# 1. 检查Mosquitto是否运行
systemctl status mosquitto

# 2. 检查端口是否监听
netstat -tulpn | grep 1883

# 3. 测试MQTT连接
mosquitto_sub -h 8.135.238.216 -p 1883 -u admin -P admin -t '#' -v

# 4. 检查防火墙
ufw status
```

### Q2: Web界面访问不了

**检查清单：**
```bash
# 1. 检查Node.js服务
pm2 status
pm2 logs esp32-cloud

# 2. 检查端口
netstat -tulpn | grep 3000

# 3. 测试本地访问
curl http://localhost:3000

# 4. 检查防火墙
ufw status
```

### Q3: 图片传输中断

**原因：**
- MQTT消息太大
- 网络不稳定
- ESP32内存不足

**解决方案：**
```cpp
// 在ESP32代码中增加缓冲区
mqttClient.setBufferSize(4096);

// 在server.js中减小分块大小
const chunkSize = 500; // 从1000改为500
```

### Q4: 如何查看日志

```bash
# 云端日志
pm2 logs esp32-cloud

# MQTT日志
tail -f /var/log/mosquitto/mosquitto.log

# Nginx日志
tail -f /var/log/nginx/access.log
tail -f /var/log/nginx/error.log

# ESP32日志
# 通过Arduino IDE串口监视器查看
```

---

## 性能优化

### 1. 启用MQTT QoS

在ESP32代码中：
```cpp
mqttClient.subscribe(subTopic.c_str(), 2); // QoS 2
```

### 2. 增加MQTT缓冲区

在ESP32代码中：
```cpp
mqttClient.setBufferSize(8192); // 增加到8KB
```

### 3. 优化图片处理

在Web前端使用Web Worker处理图片，避免阻塞UI。

### 4. 添加CDN

将静态资源（CSS、JS）放到CDN加速。

---

## 安全建议

1. **修改MQTT密码**：
   ```bash
   mosquitto_passwd /etc/mosquitto/passwd admin
   systemctl restart mosquitto
   ```

2. **使用MQTT over TLS**：
   配置SSL证书，使用8883端口

3. **限制访问IP**：
   ```bash
   ufw allow from 你的IP to any port 3000
   ```

4. **定期更新**：
   ```bash
   apt update && apt upgrade
   npm update
   ```

---

## 下一步

- [ ] 添加用户认证系统
- [ ] 支持多用户管理多设备
- [ ] 添加设备分组功能
- [ ] 添加图片历史记录
- [ ] 支持定时推送
- [ ] 添加设备监控仪表板
- [ ] 移动端App开发
