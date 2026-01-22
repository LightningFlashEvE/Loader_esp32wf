# ESP32-C3 墨水屏云端控制系统

通过云服务器和MQTT协议远程控制ESP32-C3墨水屏显示图片。

## 系统架构

```
用户浏览器 <--> 云服务器 (Web + MQTT) <--> ESP32-C3 (MQTT客户端) <--> 墨水屏
```

### 工作流程

1. 用户通过浏览器访问云服务器的Web界面
2. 上传图片并选择处理算法（支持三种算法：Floyd-Steinberg抖动、梯度边界混合、灰阶颜色映射）
3. 云服务器处理图片并转换为E6六色格式（4bit打包）
4. 云服务器通过MQTT将处理后的数据发送给ESP32-C3
5. ESP32-C3接收MQTT消息并驱动墨水屏显示

## 硬件要求

### ESP32-C3 开发板
- **推荐**：合宙 ESP32C3-CORE（4MB Flash）
- **Flash容量**：4MB（用于固件和SPIFFS存储）
- **WiFi**：2.4GHz 802.11 b/g/n

### 墨水屏
- **型号**：7.3寸 E6 彩色电子纸（800x480分辨率）
- **颜色**：6色（黑、白、黄、红、蓝、绿）
- **注意**：橙色未使用（官方驱动中已注释）

## 引脚连接

### 墨水屏连接（ESP32-C3）

| 功能 | GPIO | 说明 |
|------|------|------|
| **SCK** | **GPIO2** | SPI时钟 |
| **MOSI/DIN** | **GPIO3** | SPI数据输出 |
| **CS** | **GPIO4** | SPI片选（低电平有效） |
| **RST** | **GPIO6** | 复位信号（低电平复位） |
| **DC** | **GPIO7** | 数据/命令选择（高=数据，低=命令） |
| **BUSY** | **GPIO8** | 忙信号（输入，低电平=忙碌） |

### 系统初始化引脚

| 功能 | GPIO | 说明 | 初始状态 |
|------|------|------|----------|
| **GPIO12** | **GPIO12** | 用户自定义 | **低电平** |
| **GPIO13** | **GPIO13** | 用户自定义 | **低电平** |

**注意**：这两个引脚在系统初始化时自动设置为输出模式并输出低电平。

### 系统保留引脚（ESP32-C3-CORE）

| 功能 | GPIO | 说明 | 备注 |
|------|------|------|------|
| **UART0_TX** | GPIO21 | 串口发送（下载/调试） | 系统使用 |
| **UART0_RX** | GPIO20 | 串口接收（下载/调试） | 系统使用 |
| **Flash SPI** | GPIO14-17 | 系统Flash（4MB） | 系统使用，不可占用 |

## 目录结构

```
.
├── cloud_server/              # 云端服务器（Python Flask + Nginx）
│   ├── backend/              # Flask后端API
│   │   ├── app.py            # 主应用
│   │   ├── config.py         # 配置管理
│   │   ├── six_color_epd.py  # 六色图像处理算法（支持三种算法）
│   │   └── requirements.txt  # Python依赖
│   ├── frontend/             # Web前端
│   │   ├── index.html        # 主页面
│   │   ├── control.html      # 设备控制页
│   │   ├── app.js            # 前端逻辑
│   │   └── nginx.conf        # Nginx配置
│   └── docker-compose.yml    # Docker部署配置
│
├── Loader_esp32wf.ino         # ESP32主程序
├── mqtt_config.h              # MQTT配置和处理
├── wifi_config.h              # WiFi配网功能
├── DEV_Config.h/cpp           # 硬件配置（引脚定义）
├── epd.h                      # 墨水屏驱动接口
├── epd7in3.h                  # 7.3寸E6驱动适配层
├── EPD_7in3e.h/cpp            # 官方Demo驱动
├── GUI_Paint.h/cpp            # GUI绘制库
├── fonts.h                    # 字库头文件
├── font24.cpp                 # 24像素字体数据
├── font12.cpp                 # 12像素字体数据
├── partitions.csv             # Flash分区表
└── README.md                  # 本文件
```

## 部署步骤

### 1. 准备云服务器

#### 要求
- Linux服务器 (Ubuntu 20.04+ 推荐)
- 公网IP或域名
- Python 3.8+
- MongoDB（用于设备管理）
- **EMQX MQTT Broker**（推荐使用EMQX）

#### 安装EMQX MQTT Broker

```bash
# 下载并安装EMQX
wget https://www.emqx.com/en/downloads/broker/5.0.0/emqx-5.0.0-ubuntu20.04-amd64.deb
sudo apt install ./emqx-5.0.0-ubuntu20.04-amd64.deb

# 启动服务
sudo systemctl start emqx
sudo systemctl enable emqx

# 访问Web管理界面
# http://你的服务器IP:18083
# 默认用户名: admin
# 默认密码: public（首次登录会要求修改）
```

#### 配置EMQX授权规则

在EMQX Web管理界面中：
1. 进入 **访问控制** -> **授权** -> **内置数据库**
2. 创建规则：允许所有主题（测试用）
   - 权限：允许
   - 操作：全部
   - 主题：`#`

#### 安装MongoDB

```bash
# Ubuntu/Debian
sudo apt install mongodb

# 启动服务
sudo systemctl start mongodb
sudo systemctl enable mongodb
```

### 2. 部署云端服务

```bash
# 1. 上传代码到服务器
cd /opt
sudo git clone <your-repo> esp32-cloud
cd esp32-cloud/cloud_server

# 2. 使用Docker部署（推荐）
# 注意：使用 docker compose（无连字符），这是新版本Docker的标准命令

# 首次部署或更新代码后：
cd /opt/cloud_server  # 或你的项目路径
git pull  # 拉取最新代码
docker compose build --no-cache  # 重新构建镜像
docker compose up -d --force-recreate  # 强制重新创建容器

# 查看服务状态
docker compose ps

# 查看日志
docker compose logs -f backend  # 后端日志
docker compose logs -f frontend  # 前端日志

# 或手动部署：
cd backend
pip3 install -r requirements.txt

# 3. 配置环境变量
# 编辑 docker-compose.yml 或设置环境变量：
export MQTT_BROKER=8.135.238.216
export MQTT_PORT=1883
export MQTT_USER=admin
export MQTT_PASS=admin
export MONGODB_URI=mongodb://localhost:27017/esp32_epd

# 4. 初始化数据库索引
python3 create_indexes.py

# 5. 启动服务
python3 app.py
```

### 3. 配置防火墙

```bash
# 开放端口
sudo ufw allow 80/tcp      # Web服务（Nginx）
sudo ufw allow 5000/tcp    # Flask后端（如果直接访问）
sudo ufw allow 1883/tcp    # MQTT
sudo ufw allow 18083/tcp   # EMQX管理界面
sudo ufw enable
```

### 4. 烧录ESP32-C3程序

#### Arduino IDE配置

1. **安装ESP32开发板支持**：
   - 文件 -> 首选项 -> 附加开发板管理器网址：
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - 工具 -> 开发板 -> 开发板管理器 -> 搜索 "esp32" -> 安装

2. **选择开发板**：
   - 工具 -> 开发板 -> ESP32 Arduino -> **ESP32C3 Dev Module**
   - 工具 -> Partition Scheme -> **Custom Partition Table**
   - 工具 -> Flash Size -> **4MB (32Mb)**
   - 工具 -> Flash Frequency -> **80MHz**
   - 工具 -> Flash Mode -> **DIO**
   - 工具 -> Upload Speed -> **921600**

3. **安装依赖库**：
   - PubSubClient (by Nick O'Leary)
   - ArduinoJson (by Benoit Blanchon)

4. **修改配置**：

打开 `mqtt_config.h`，修改：

```cpp
// MQTT配置
#define MQTT_HOST "8.135.238.216"  // 改成你的云服务器IP
#define MQTT_PORT 1883
#define MQTT_USER "admin"
#define MQTT_PASS "admin"
```

5. **分区表配置**：

项目根目录的 `partitions.csv` 已配置好：
```
nvs,      data, nvs,     0x9000,  0x5000,
phy_init, data, phy,     0xe000,  0x2000,
factory,  app,  factory, 0x10000, 0x150000,
spiffs,   data, spiffs,  0x250000, 0x1B0000,
```

**注意**：确保 Arduino IDE 中选择了 **Custom Partition Table**，这样会自动使用项目根目录的 `partitions.csv`。

6. **编译上传**：
   - 工具 -> Erase Flash -> **All Flash Contents**（首次烧录建议完全擦除）
   - 点击上传

### 5. WiFi配网

设备支持两种WiFi连接方式：

#### 方式1：AP配网模式（首次使用）

1. 设备启动后，如果未配置WiFi，会自动进入AP模式
2. 设备会创建一个WiFi热点：`EPD-XXXXXX`（XXXXXX为设备码）
3. 用手机/电脑连接此热点（无密码）
4. 访问 `http://192.168.4.1`
5. 输入WiFi名称和密码，点击连接
6. 设备会自动重启并连接WiFi

#### 方式2：代码配置（开发测试）

在 `wifi_config.h` 中可以设置默认WiFi（仅用于开发测试）。

### 6. 测试系统

1. **查看ESP32串口输出**：
```
========================================
  WiFi配网初始化
========================================
✅ WiFi已连接
IP地址: 192.168.10.143
========================================
  MQTT云端控制模式
========================================
完整MAC地址: 3C:8A:1F:B6:DA:20
设备码模式: 后6位
⭐ 设备码: B6DA20
MQTT服务器: 8.135.238.216:1883
========================================
✅ MQTT已连接
订阅主题: dev/B6DA20/down/epd
✅ 系统就绪，等待云端命令...
```

2. **访问Web界面**：
   - 打开浏览器访问：`http://你的服务器IP` 或 `http://your-domain.com`
   - 首次访问需要注册/登录

3. **添加设备**：
   - 登录后进入设备管理页面
   - 点击"添加设备"
   - 输入ESP32显示的设备码（例如：`B6DA20`）
   - 设备会自动绑定

4. **上传图片**：
   - 在设备控制页面选择已添加的设备
   - 选择墨水屏型号：**7.3寸 E6**
   - 拖拽或选择图片
   - 切换到"处理"面板
   - 选择处理算法：
     - **Floyd-Steinberg抖动**：适合渐变和细节丰富的图像，使用误差扩散技术
     - **梯度边界混合**：适合线条和边界明显的图像，在边界区域进行颜色混合
     - **灰阶与颜色映射**：将灰度映射到颜色梯度（黑->蓝->红->绿->黄->白），纯色块效果
   - 点击"处理并预览"查看效果
   - 点击"上传到设备"
   - 墨水屏会显示处理后的图片

## MQTT主题说明

### 下行主题（云端 -> ESP32）

- `dev/{deviceId}/down/epd` - EPD控制命令

命令格式：
```json
// 初始化EPD
{"cmd": "EPD", "type": 0, "timestamp": 1234567890}

// 加载数据（小图片直接传输）
{"cmd": "LOAD", "data": [0,255,128,...], "length": 1000, "timestamp": 1234567890}

// 下载命令（大图片通过HTTP下载）
{"cmd": "DOWNLOAD", "url": "http://...", "timestamp": 1234567890}

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
  "deviceId": "B6DA20",
  "rssi": -45,
  "ip": "192.168.10.143",
  "uptime_ms": 12345678,
  "freeHeap": 200000
}
```

## Flash存储说明

### 分区表配置

项目使用自定义分区表（`partitions.csv`）：

| 分区 | 类型 | 偏移 | 大小 | 说明 |
|------|------|------|------|------|
| nvs | DATA | 0x9000 | 20KB | NVS存储（WiFi配置、设备绑定状态） |
| phy_init | DATA | 0xE000 | 8KB | PHY初始化数据 |
| factory | APP | 0x10000 | 1344KB | 应用程序 |
| spiffs | DATA | 0x250000 | 1728KB | SPIFFS文件系统（图片缓存） |

### SPIFFS使用

- **用途**：存储临时图片数据（避免内存不足）
- **自动格式化**：首次使用时自动格式化
- **文件路径**：`/temp_image.bin`

## 设备码说明

设备码基于MAC地址生成，支持三种模式（在 `mqtt_config.h` 中配置）：

- **模式0**：完整12位（例如：`3C8A1FB6DA20`）
- **模式1**：前6位（例如：`3C8A1F`）
- **模式2**：后6位（例如：`B6DA20`）**（默认）**

## 故障排查

### ESP32无法连接WiFi

- 检查WiFi SSID和密码是否正确
- 确认WiFi信号强度（2.4GHz）
- 查看串口输出的错误信息
- 尝试使用AP配网模式重新配置

### ESP32无法连接MQTT

- 检查服务器IP和端口（默认1883）
- 确认防火墙已开放1883端口
- 检查MQTT用户名密码
- 确认EMQX服务正在运行
- 检查EMQX授权规则是否允许连接

### SPIFFS挂载失败

- **首次使用**：这是正常现象，会自动格式化
- **持续失败**：
  1. 确认分区表已正确烧录
  2. 在Arduino IDE中：工具 -> Erase Flash -> All Flash Contents
  3. 重新编译并烧录

### 图片无法上传

- 打开浏览器开发者工具查看网络请求
- 检查设备ID是否正确
- 确认ESP32在线并已连接MQTT
- 查看云服务器日志
- 检查设备是否已绑定

### NVS错误

- `nvs_open failed: NOT_FOUND` - 首次使用时正常，会自动创建命名空间
- 如果持续出现，检查Flash是否损坏

## 图像处理算法

系统支持三种图像处理算法，用户可以在Web界面中选择：

### 1. Floyd-Steinberg 抖动算法

- **特点**：误差扩散技术，适合渐变和细节丰富的图像
- **原理**：将量化误差扩散到相邻像素，产生平滑的视觉过渡
- **适用场景**：照片、渐变图像、细节丰富的图像

### 2. 梯度边界混合算法

- **特点**：基于Sobel梯度检测边界，在边界区域进行颜色混合
- **原理**：检测图像边界，在边界区域混合颜色以减少量化伪影
- **适用场景**：线条图、边界明显的图像、文字图像
- **参数**：梯度阈值（10-100，默认40），可调整边界检测敏感度

### 3. 灰阶与颜色映射算法

- **特点**：将灰度图映射到自定义颜色梯度，产生纯色块效果
- **原理**：将256级灰度均匀映射到6种E6颜色，无抖动
- **颜色梯度**：黑 -> 蓝 -> 红 -> 绿 -> 黄 -> 白（按亮度顺序）
- **适用场景**：需要纯色块效果的图像、简化图像
- **优化**：
  - 非均匀分段，确保深红色和黄色正确识别
  - 深红色范围：71-120（避免被映射为蓝色）
  - 黄色范围：181-235（避免被映射为白色）

## 扩展功能

### 已实现功能

- ✅ WiFi AP配网模式
- ✅ 设备绑定管理
- ✅ 图片HTTP下载（大图片）
- ✅ SPIFFS图片缓存
- ✅ 设备状态上报
- ✅ 多用户支持
- ✅ 三种图像处理算法（Floyd-Steinberg抖动、梯度边界混合、灰阶颜色映射）
- ✅ 算法选择界面和实时预览

### 可扩展功能

1. **OTA升级**：通过MQTT推送固件更新
2. **定时任务**：支持定时推送图片到设备
3. **批量控制**：支持同时向多个设备推送图片
4. **图片历史**：保存用户上传的图片，支持快速重新发送
5. **更多图片处理选项**：
   - 更多抖动算法（Atkinson, Burkes等）
   - 自动裁剪和对齐
   - 文字叠加
   - 二维码生成

## 许可证

基于原Waveshare项目修改，保留原作者版权信息。

## 参考资料

- [ESP32-C3技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_cn.pdf)
- [合宙ESP32C3-CORE开发板](https://wiki.luatos.com/chips/esp32c3/board.html)
- [PubSubClient库文档](https://pubsubclient.knolleary.net/)
- [ArduinoJson文档](https://arduinojson.org/)
- [EMQX文档](https://www.emqx.io/docs/)
