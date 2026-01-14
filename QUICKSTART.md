# 快速上手指南

## 最快5分钟部署

### 前提条件
- 一台Ubuntu 20.04+ 云服务器
- 一个ESP32开发板 + 墨水屏
- Arduino IDE

---

## 步骤1：云服务器部署（2分钟）

SSH连接到你的服务器：
```bash
ssh root@8.135.238.216
```

下载并运行安装脚本：
```bash
# 创建项目目录
mkdir -p /opt/esp32-cloud
cd /opt/esp32-cloud

# 将cloud_server文件夹上传到这里
# 可以使用scp、git或直接复制

cd cloud_server

# 赋予执行权限
chmod +x install.sh

# 运行安装脚本
bash install.sh
```

安装脚本会自动：
- ✅ 安装MQTT Broker (Mosquitto)
- ✅ 安装Node.js
- ✅ 安装项目依赖
- ✅ 配置防火墙
- ✅ 启动服务

---

## 步骤2：配置ESP32（2分钟）

### 2.1 安装Arduino库

在Arduino IDE中安装以下库：
1. 打开 `工具` -> `管理库`
2. 搜索并安装：
   - `PubSubClient` (by Nick O'Leary)
   - `ArduinoJson` (by Benoit Blanchon) - 版本6.x

### 2.2 准备代码

1. 将以下文件复制到 `esp32_mqtt_epd/` 文件夹：
   ```
   原项目根目录下的文件:
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
   ```

2. 修改 `esp32_mqtt_epd.ino` 配置：
   ```cpp
   const char* WIFI_SSID     = "你的WiFi名称";
   const char* WIFI_PASSWORD = "你的WiFi密码";
   const char* MQTT_HOST = "8.135.238.216";  // 改成你的服务器IP
   ```

### 2.3 上传代码

1. 连接ESP32到电脑
2. 在Arduino IDE中：
   - 选择板子：`ESP32 Dev Module`
   - 选择正确的串口
   - 点击"上传"

### 2.4 获取设备ID

上传完成后：
1. 打开串口监视器（波特率：115200）
2. 等待ESP32连接WiFi和MQTT
3. 记录显示的设备ID，例如：
   ```
   设备ID: C3-7CDFA1B2C3D4
   ```

---

## 步骤3：测试（1分钟）

### 3.1 访问Web界面

浏览器打开：
```
http://8.135.238.216:3000
```

### 3.2 上传图片

1. **输入设备ID**：填入ESP32显示的设备ID（如：`C3-7CDFA1B2C3D4`）

2. **选择墨水屏型号**：
   - 1.54" 200x200 → 选择 `1.54" 200x200`
   - 2.13" 122x250 → 选择 `2.13" 122x250`
   - 其他型号类推

3. **上传图片**：
   - 拖拽图片到虚线框，或点击"选择图片"按钮
   - 等待图片加载

4. **处理图片**：
   - 点击"处理图片"按钮
   - 在右侧看到黑白预览

5. **上传到设备**：
   - 点击"上传到设备"按钮
   - 观察ESP32串口输出
   - 等待墨水屏刷新（约10-30秒）

---

## 成功标志

### 云端
浏览器状态栏显示：
```
[时间] ✅ 上传完成！
```

### ESP32串口输出
```
📥 收到消息: dev/C3-7CDFA1B2C3D4/down/epd
命令: EPD
初始化EPD类型: 0
✅ EPD初始化完成

📥 收到消息: dev/C3-7CDFA1B2C3D4/down/epd
命令: LOAD
接收数据: 1000 字节
...

📥 收到消息: dev/C3-7CDFA1B2C3D4/down/epd
命令: SHOW
刷新显示
✅ 显示完成
```

### 墨水屏
显示上传的图片！🎉

---

## 常见问题

### Q: ESP32无法连接WiFi
**A**: 检查SSID和密码是否正确，确认WiFi信号强度

### Q: ESP32无法连接MQTT
**A**: 
```bash
# 在服务器上检查MQTT服务
systemctl status mosquitto

# 检查防火墙
ufw status

# 测试MQTT
mosquitto_sub -h localhost -p 1883 -u admin -P admin -t '#'
```

### Q: Web界面无法访问
**A**:
```bash
# 检查服务状态
pm2 status

# 查看日志
pm2 logs esp32-cloud

# 重启服务
pm2 restart esp32-cloud
```

### Q: 图片上传失败
**A**:
1. 确认设备ID正确
2. 确认ESP32已连接MQTT（串口显示"✅ MQTT已连接"）
3. 打开浏览器开发者工具查看网络请求
4. 检查云端日志：`pm2 logs esp32-cloud`

---

## 视频教程

TODO: 添加视频链接

---

## 下一步

- 📖 阅读完整文档：[README.md](README.md)
- 🚀 深入部署：[DEPLOYMENT.md](DEPLOYMENT.md)
- 💡 查看API文档
- 🔧 自定义配置

---

## 需要帮助？

- 查看日志：`pm2 logs`
- 检查MQTT：`mosquitto_sub -h localhost -p 1883 -u admin -P admin -t '#' -v`
- 查看防火墙：`ufw status`
- 测试网络：`ping 8.135.238.216`

---

祝你使用愉快！🎉
