# ESP32 墨水屏云端控制系统

通过云服务器和MQTT协议远程控制ESP32墨水屏显示图片。

## 系统架构

```
用户浏览器 <--> 云服务器 (Web + MQTT) <--> ESP32 (MQTT客户端) <--> 墨水屏
```

### 工作流程

1. 用户通过浏览器访问云服务器的Web界面
2. 上传图片并进行处理（缩放、抖动、格式转换）
3. 云服务器通过MQTT将处理后的数据发送给ESP32
4. ESP32接收MQTT消息并驱动墨水屏显示

## 目录结构

```
.
├── cloud_server/           # 云端服务器
│   ├── server.js          # Node.js后端服务
│   ├── package.json       # 依赖配置
│   ├── .env.example       # 环境变量示例
│   └── public/            # Web前端
│       ├── index.html     # 主页面
│       └── app.js         # 前端逻辑
│
├── esp32_mqtt_epd/        # ESP32代码
│   ├── esp32_mqtt_epd.ino # 主程序
│   └── mqtt_epd_handler.h # MQTT命令处理
│
└── 原有的EPD驱动文件
    ├── epd.h              # 墨水屏驱动
    ├── buff.h             # 数据缓冲
    └── epd*.h             # 各型号墨水屏驱动
```

## 部署步骤

### 1. 准备云服务器

#### 要求
- Linux服务器 (Ubuntu 20.04+ 推荐)
- 公网IP或域名
- Node.js 16+
- MQTT Broker (Mosquitto 或 EMQX)

#### 安装MQTT Broker (Mosquitto)

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install mosquitto mosquitto-clients

# 启动服务
sudo systemctl start mosquitto
sudo systemctl enable mosquitto

# 配置认证（可选）
sudo mosquitto_passwd -c /etc/mosquitto/passwd admin
# 输入密码: admin

# 编辑配置文件
sudo nano /etc/mosquitto/mosquitto.conf
```

添加以下配置：
```
listener 1883
allow_anonymous false
password_file /etc/mosquitto/passwd
```

重启服务：
```bash
sudo systemctl restart mosquitto
```

#### 安装Node.js

```bash
# 使用NodeSource仓库
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt install -y nodejs

# 验证安装
node --version
npm --version
```

### 2. 部署云端服务

```bash
# 1. 上传代码到服务器
cd /opt
sudo git clone <your-repo> esp32-cloud
cd esp32-cloud/cloud_server

# 2. 安装依赖
npm install

# 3. 配置环境变量
cp .env.example .env
nano .env
```

编辑 `.env` 文件：
```env
PORT=3000
MQTT_BROKER=mqtt://localhost:1883
MQTT_USER=admin
MQTT_PASS=admin
```

```bash
# 4. 测试运行
npm start

# 5. 使用PM2管理进程（生产环境）
sudo npm install -g pm2
pm2 start server.js --name esp32-cloud
pm2 save
pm2 startup
```

### 3. 配置防火墙

```bash
# 开放端口
sudo ufw allow 3000/tcp   # Web服务
sudo ufw allow 1883/tcp   # MQTT
sudo ufw enable
```

### 4. 配置Nginx反向代理（可选）

```bash
sudo apt install nginx

# 创建配置文件
sudo nano /etc/nginx/sites-available/esp32-cloud
```

内容：
```nginx
server {
    listen 80;
    server_name your-domain.com;  # 改为你的域名或IP

    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }
}
```

启用配置：
```bash
sudo ln -s /etc/nginx/sites-available/esp32-cloud /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

### 5. 烧录ESP32程序

#### Arduino IDE配置

1. 安装依赖库：
   - PubSubClient (by Nick O'Leary)
   - ArduinoJson (by Benoit Blanchon)

2. 修改配置：

打开 `esp32_mqtt_epd/esp32_mqtt_epd.ino`，修改：

```cpp
// WiFi配置
const char* WIFI_SSID     = "你的WiFi名称";
const char* WIFI_PASSWORD = "你的WiFi密码";

// MQTT配置
const char* MQTT_HOST = "你的服务器IP";  // 例如: "8.135.238.216"
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER = "admin";
const char* MQTT_PASS = "admin";
```

3. 整合原有代码：

将原项目中的以下文件复制到 `esp32_mqtt_epd/` 文件夹：
```
epd.h
buff.h
epd*.h (所有墨水屏驱动文件)
```

4. 编译上传：
   - 选择开发板：ESP32 Dev Module
   - 选择端口
   - 点击上传

### 6. 测试系统

1. **查看ESP32串口输出**：
```
========================================
  ESP32 E-Paper MQTT Client
========================================
✅ SPI已初始化
连接WiFi...
✅ WiFi已连接
IP地址: 192.168.1.100
设备ID: C3-7CDFA1B2C3D4
正在连接MQTT...
✅ MQTT已连接
订阅主题: dev/C3-7CDFA1B2C3D4/down/#
========================================
✅ 系统就绪，等待命令...
========================================
```

2. **访问Web界面**：
   - 打开浏览器访问：`http://你的服务器IP:3000`
   - 或使用域名：`http://your-domain.com`

3. **上传图片**：
   - 在设备ID框输入ESP32显示的设备ID（例如：`C3-7CDFA1B2C3D4`）
   - 选择墨水屏型号
   - 拖拽或选择图片
   - 点击"处理图片"
   - 点击"上传到设备"

4. **观察效果**：
   - ESP32串口会显示接收到的命令
   - 墨水屏应该显示上传的图片

## MQTT主题说明

### 下行主题（云端 -> ESP32）

- `dev/{deviceId}/down/epd` - EPD控制命令

命令格式：
```json
// 初始化EPD
{"cmd": "EPD", "type": 0, "timestamp": 1234567890}

// 加载数据
{"cmd": "LOAD", "data": [0,255,128,...], "length": 1000, "timestamp": 1234567890}

// 切换通道
{"cmd": "NEXT", "timestamp": 1234567890}

// 显示
{"cmd": "SHOW", "timestamp": 1234567890}
```

### 上行主题（ESP32 -> 云端）

- `dev/{deviceId}/up/status` - 设备状态上报

格式：
```json
{
  "deviceId": "C3-7CDFA1B2C3D4",
  "rssi": -45,
  "ip": "192.168.1.100",
  "uptime_ms": 12345678,
  "freeHeap": 200000
}
```

## 故障排查

### ESP32无法连接WiFi
- 检查SSID和密码是否正确
- 确认WiFi信号强度
- 查看串口输出的错误信息

### ESP32无法连接MQTT
- 检查服务器IP和端口
- 确认防火墙已开放1883端口
- 检查MQTT用户名密码
- 使用MQTT客户端测试连接：
  ```bash
  mosquitto_sub -h 你的IP -p 1883 -u admin -P admin -t '#' -v
  ```

### Web界面无法访问
- 检查服务器是否运行：`pm2 status`
- 检查端口是否开放：`sudo netstat -tulpn | grep 3000`
- 查看服务日志：`pm2 logs esp32-cloud`

### 图片无法上传
- 打开浏览器开发者工具查看网络请求
- 检查设备ID是否正确
- 确认ESP32在线并已连接MQTT
- 查看云服务器日志

## 扩展功能

### 1. 添加设备管理
可以在云端添加数据库（MongoDB/MySQL）存储设备信息和历史记录。

### 2. 添加图片历史
保存用户上传的图片，支持快速重新发送。

### 3. 定时任务
支持定时推送图片到设备。

### 4. 批量控制
支持同时向多个设备推送图片。

### 5. 更多图片处理选项
- 更多抖动算法（Atkinson, Burkes等）
- 自动裁剪和对齐
- 文字叠加
- 二维码生成

## 许可证

基于原Waveshare项目修改，保留原作者版权信息。

## 作者

- 原始项目：Waveshare Team
- 云端扩展：[Your Name]

## 参考资料

- [PubSubClient库文档](https://pubsubclient.knolleary.net/)
- [ArduinoJson文档](https://arduinojson.org/)
- [EMQX文档](https://www.emqx.io/docs/)
- [Mosquitto文档](https://mosquitto.org/documentation/)
