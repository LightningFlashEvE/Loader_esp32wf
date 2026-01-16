# 项目结构说明

```
Loader_esp32wf/                      # 原项目根目录
│
├── 固件文件
│   ├── Loader_esp32wf.ino          # 主程序（MQTT云端控制）
│   ├── mqtt_config.h               # MQTT配置和处理
│   ├── epd.h                        # 墨水屏驱动核心
│   ├── epd7in3.h                    # 7.3" E6 驱动适配层
│   ├── EPD_7in3e.h/cpp             # 7.3" E6 官方驱动
│   ├── DEV_Config.h/cpp            # 硬件配置（官方Demo）
│   ├── GUI_Paint.h/cpp             # GUI绘制库（官方Demo）
│   ├── fonts.h                      # 字库头文件
│   ├── font12.cpp, font24.cpp      # 字体数据
│   ├── Debug.h                      # 调试宏
│   ├── buff.h                       # 数据缓冲管理
│   └── [其他驱动文件已删除，仅保留7.3" E6]
│
├── cloud_server/                    # 【新增】云端服务器
│   ├── package.json                # Node.js依赖配置
│   ├── server.js                   # Express服务器 + MQTT客户端
│   ├── .env.example                # 环境变量示例
│   ├── Dockerfile                  # Docker镜像配置
│   ├── install.sh                  # 一键安装脚本
│   └── public/                     # Web前端
│       ├── index.html              # 主页面（改进的UI）
│       └── app.js                  # 前端逻辑（MQTT通信）
│
├── esp32_mqtt_epd/                 # 【新增】ESP32 MQTT客户端
│   ├── esp32_mqtt_epd.ino         # 主程序（MQTT + EPD）
│   ├── mqtt_epd_handler.h         # MQTT命令处理器
│   └── [需复制原项目的epd*.h]    # 从原项目复制驱动文件
│
├── mosquitto/                      # 【新增】MQTT配置
│   ├── config/
│   │   └── mosquitto.conf         # Mosquitto配置
│   └── setup.sh                   # 初始化脚本
│
├── nginx/                          # 【新增】Nginx配置
│   └── nginx.conf                 # 反向代理配置
│
├── docker-compose.yml              # 【新增】Docker编排
│
└── 文档
    ├── README.md                   # 【新增】完整文档
    ├── DEPLOYMENT.md               # 【新增】部署指南
    ├── QUICKSTART.md               # 【新增】快速开始
    └── PROJECT_STRUCTURE.md        # 本文件
```

## 文件说明

### 云端服务器 (cloud_server/)

#### server.js
- **功能**：Node.js + Express Web服务器
- **职责**：
  - 提供Web界面
  - 连接MQTT Broker
  - 接收浏览器请求
  - 转发命令到ESP32
- **API端点**：
  - `GET /` - Web界面
  - `GET /api/devices` - 获取设备列表
  - `POST /api/epd/init` - 初始化墨水屏
  - `POST /api/epd/load` - 加载数据
  - `POST /api/epd/next` - 切换通道
  - `POST /api/epd/show` - 显示图片

#### public/index.html
- **功能**：用户界面
- **特性**：
  - 现代化设计
  - 拖拽上传图片
  - 实时预览
  - 设备选择
  - 参数调整

#### public/app.js
- **功能**：前端逻辑
- **职责**：
  - 图片处理（缩放、抖动、格式转换）
  - 与服务器API通信
  - 进度显示
  - 错误处理

### ESP32客户端 (esp32_mqtt_epd/)

#### esp32_mqtt_epd.ino
- **功能**：ESP32主程序
- **职责**：
  - WiFi连接
  - MQTT客户端
  - 接收云端命令
  - 驱动墨水屏
  - 状态上报

#### mqtt_epd_handler.h
- **功能**：MQTT命令处理器
- **实现的命令**：
  - `EPD` - 初始化墨水屏
  - `LOAD` - 加载图片数据
  - `NEXT` - 切换颜色通道
  - `SHOW` - 刷新显示

### MQTT配置 (mosquitto/)

#### config/mosquitto.conf
- MQTT Broker配置
- 端口：1883（MQTT）、9001（WebSocket）
- 认证配置
- 日志配置

### Docker (docker-compose.yml)

容器编排：
- **mosquitto**：MQTT Broker
- **esp32-cloud**：Web服务
- **nginx**：反向代理（可选）

## 数据流

### 上传图片流程

```
1. 用户浏览器
   ↓ [上传图片]
   
2. 前端 (app.js)
   - 读取图片
   - 处理图片（缩放、抖动、转换）
   - 生成字节数组
   ↓ [HTTP POST]
   
3. 云端服务器 (server.js)
   - 接收HTTP请求
   - 验证数据
   - 发布MQTT消息
   ↓ [MQTT Publish]
   
4. MQTT Broker (Mosquitto)
   - 接收消息
   - 转发给订阅者
   ↓ [MQTT Subscribe]
   
5. ESP32 (esp32_mqtt_epd.ino)
   - 接收MQTT消息
   - 解析JSON
   - 调用EPD驱动
   ↓ [SPI]
   
6. 墨水屏
   - 显示图片
```

### 状态上报流程

```
1. ESP32
   - 定期收集状态（RSSI、IP、内存等）
   - 生成JSON
   ↓ [MQTT Publish]
   
2. MQTT Broker
   - 转发消息
   ↓ [MQTT Subscribe]
   
3. 云端服务器
   - 接收状态
   - 更新设备列表
   - （可选）存储到数据库
   ↓ [WebSocket]
   
4. 用户浏览器
   - 实时显示设备状态
```

## MQTT主题设计

### 命名规范
```
dev/{deviceId}/{direction}/{type}
```

- `deviceId`：设备唯一ID（MAC地址）
- `direction`：`up`（上行）或 `down`（下行）
- `type`：消息类型

### 主题列表

#### 下行（云 → ESP32）
```
dev/C3-XXXXXXXXXXXX/down/epd        # EPD控制命令
dev/C3-XXXXXXXXXXXX/down/config     # 配置更新（预留）
dev/C3-XXXXXXXXXXXX/down/ota        # OTA更新（预留）
```

#### 上行（ESP32 → 云）
```
dev/C3-XXXXXXXXXXXX/up/status       # 状态上报
dev/C3-XXXXXXXXXXXX/up/log          # 日志上报（预留）
dev/C3-XXXXXXXXXXXX/up/alert        # 告警（预留）
```

### 消息格式

所有消息使用JSON格式：

```json
// EPD初始化
{
  "cmd": "EPD",
  "type": 0,
  "timestamp": 1234567890
}

// 加载数据
{
  "cmd": "LOAD",
  "data": [0, 255, 128, ...],
  "length": 1000,
  "timestamp": 1234567890
}

// 状态上报
{
  "deviceId": "C3-XXXXXXXXXXXX",
  "rssi": -45,
  "ip": "192.168.1.100",
  "uptime_ms": 12345678,
  "freeHeap": 200000
}
```

## 与原项目的关系

### 保留的组件
- ✅ `epd.h` 及所有 `epd*.h` - 墨水屏驱动
- ✅ `buff.h` - 数据缓冲管理
- ✅ 图片处理算法（Floyd-Steinberg抖动等）

### 替换的组件
- ❌ `Loader_esp32wf.ino` → `esp32_mqtt_epd.ino`
  - TCP服务器 → MQTT客户端
  
- ❌ `srvr.h` → 已删除（不再需要本地Web服务器）
  - ESP32上的HTTP服务器 → 云端Express服务器
  
- ❌ `html.h` + `css.h` + `scripts.h` → 已删除（不再需要本地Web界面，使用云端界面）
  - 分离的HTML字符串 → 独立的Web文件
  - 改进的UI/UX

### 新增的组件
- ➕ MQTT通信层
- ➕ 云端服务器
- ➕ Docker部署方案
- ➕ 完整文档

## 部署方式对比

### 原方案
```
用户浏览器 ←→ ESP32 (HTTP服务器 + 墨水屏驱动)
```
- ✅ 简单
- ✅ 本地网络即可
- ❌ 需要在同一局域网
- ❌ 每个设备独立访问
- ❌ 无法远程控制

### 新方案
```
用户浏览器 ←→ 云服务器 ←→ MQTT ←→ ESP32 (墨水屏驱动)
```
- ✅ 远程访问
- ✅ 统一管理多设备
- ✅ 可扩展（用户系统、权限等）
- ✅ 更好的UI/UX
- ❌ 需要云服务器
- ❌ 依赖网络连接

## 开发路线图

### 已完成 ✅
- [x] 云端服务器基础框架
- [x] ESP32 MQTT客户端
- [x] Web界面
- [x] 图片上传和显示
- [x] 部署文档

### 计划中 📋
- [ ] 用户认证系统
- [ ] 多设备管理
- [ ] 图片历史记录
- [ ] 定时推送
- [ ] WebSocket实时通信
- [ ] 设备监控仪表板
- [ ] OTA固件更新
- [ ] 移动端App

### 未来展望 🚀
- [ ] AI图片优化
- [ ] 模板市场
- [ ] 设备分组
- [ ] 数据分析
- [ ] API开放平台
