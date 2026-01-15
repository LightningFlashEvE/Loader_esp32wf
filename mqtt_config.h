/**
 ******************************************************************************
 * @file    mqtt_config.h
 * @author  Modified for MQTT Cloud Control
 * @version V2.0.0
 * @date    2026-01-13
 * @brief   MQTT配置和处理
 *          通过云端MQTT控制墨水屏
 ******************************************************************************
 */

#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include "buff.h"
#include "epd.h"

/* WiFi配置（如果需要覆盖srvr.h中的配置） -----------------------------------*/
// 可以使用srvr.h中定义的ssid和password，或者在这里重新定义
#ifndef WIFI_SSID
    #define WIFI_SSID "XXGF"
    #define WIFI_PASSWORD "XXGFNXXGM"
#endif

/* MQTT配置 ----------------------------------------------------------------*/
#define MQTT_HOST "8.135.238.216"  // 改成你的云服务器IP
#define MQTT_PORT 1883
#define MQTT_USER "admin"
#define MQTT_PASS "admin"

/* 设备ID配置 --------------------------------------------------------------*/
// 选择设备ID生成方式：
// 0 = 使用完整MAC地址 (12位，例如: C3-112233445566)
// 1 = 仅使用MAC地址前6位 (例如: C3-112233)
// 2 = 仅使用MAC地址后6位 (例如: C3-445566)
#define DEVICE_ID_MODE 2  // 默认使用后6位

/* 全局变量 ----------------------------------------------------------------*/
WiFiClient mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);
Preferences preferences;  // NVS持久化存储

String deviceId;
String topicDownBase;
String topicUpStatus;

unsigned long lastReportMs = 0;
const unsigned long REPORT_INTERVAL_MS = 30000;

// 设备激活状态
bool deviceActivated = false;
unsigned long deviceStartupTime = 0;
const unsigned long STARTUP_WAIT_MS = 5000;  // 启动后等待5秒
bool deviceCodeShown = false;

// 设备绑定状态（本地持久化）
bool deviceClaimed = false;
const char* PREF_NAMESPACE = "device";
const char* PREF_KEY_CLAIMED = "claimed";

// 云端API配置
#define CLOUD_API_HOST "8.135.238.216"  // 与MQTT_HOST保持一致
#define CLOUD_API_PORT 5000  // Flask默认端口
#define CLOUD_API_TIMEOUT_MS 5000  // HTTP请求超时时间

/* 简单的5x7点阵字体 (数字0-9, 字母A-F) ------------------------------------*/
const byte font5x7[][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}  // F
};

/* 在屏幕上显示设备码（使用大号数字） ----------------------------------------*/
void displayDeviceCode() {
    Serial.println("📱 开始显示设备码...");
    Serial.print("⭐ 设备码: ");
    Serial.println(deviceId);
    
    // 注意：需要先通过云端发送EPD初始化命令，或者在这里设置默认屏幕型号
    // 如果EPD_dispIndex还未设置，使用默认值
    if (EPD_dispIndex < 0 || EPD_dispIndex >= (sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]))) {
        // 默认使用7.5"B V2屏（三色）
        EPD_dispIndex = 23;
        Serial.println("⚠️  使用默认屏幕型号: 7.5\" B V2");
    }
    
    EPD_dispInit();
    
    // 获取当前屏幕的分辨率
    int width, height;
    
    // 根据屏幕型号设置分辨率（从scripts.h的epdArr获取）
    const int resolutions[][2] = {
        {200,200}, {200,200}, {152,152}, {122,250}, {104,212}, {104,212}, {104,212},
        {176,264}, {176,264}, {128,296}, {128,296}, {128,296}, {128,296},
        {400,300}, {400,300}, {400,300}, {600,448}, {600,448}, {600,448},
        {640,384}, {640,384}, {640,384}, {800,480}, {800,480}, {880,528}
    };
    
    if (EPD_dispIndex < 25) {
        width = resolutions[EPD_dispIndex][0];
        height = resolutions[EPD_dispIndex][1];
    } else {
        // 默认使用800x480
        width = 800;
        height = 480;
    }
    
    Serial.printf("屏幕分辨率: %dx%d\n", width, height);
    int bufSize = (width * height) / 8;
    
    // 清空屏幕（白色）
    EPD_SendCommand(0x10);
    for(int i = 0; i < bufSize; i++) {
        EPD_SendData(0xFF);
    }
    
    // 在屏幕上显示设备码（使用大号字符）
    EPD_SendCommand(0x13);
    
    String code = deviceId;
    int charWidth = 5;   // 字符基础宽度
    int charHeight = 7;  // 字符基础高度
    
    // 根据屏幕大小自动调整字符大小
    int scale = 10;      // 默认放大倍数
    if (width >= 800) {
        scale = 15;      // 大屏幕（原来30，缩小一倍）
    } else if (width >= 400) {
        scale = 12;
    } else if (width >= 200) {
        scale = 10;
    } else {
        scale = 6;
    }
    
    int spacing = scale / 3;  // 字符间距
    int startY = height / 2 - (charHeight * scale) / 2;  // 垂直居中
    
    // 计算起始X坐标（水平居中）
    int totalWidth = code.length() * (charWidth * scale + spacing);
    int startX = (width - totalWidth) / 2;
    if (startX < 0) startX = 10;
    
    for(int byteIdx = 0; byteIdx < bufSize; byteIdx++) {
        byte pixelByte = 0xFF; // 默认白色
        
        // 计算当前字节对应的行和列
        int row = byteIdx / (width / 8);
        int colByte = byteIdx % (width / 8);
        
        // 遍历当前字节的8个像素
        for(int bit = 0; bit < 8; bit++) {
            int x = colByte * 8 + bit;
            int y = row;
            
            // 绘制每个字符
            for(int charIdx = 0; charIdx < code.length(); charIdx++) {
                char c = code[charIdx];
                int fontIdx = -1;
                
                if(c >= '0' && c <= '9') {
                    fontIdx = c - '0';
                } else if(c >= 'A' && c <= 'F') {
                    fontIdx = 10 + (c - 'A');
                } else if(c >= 'a' && c <= 'f') {
                    fontIdx = 10 + (c - 'a');
                }
                
                if(fontIdx >= 0 && fontIdx < 16) {
                    int charX = startX + charIdx * (charWidth * scale + spacing);
                    int charY = startY;
                    
                    // 检查当前像素是否在字符范围内
                    if(x >= charX && x < charX + charWidth * scale &&
                       y >= charY && y < charY + charHeight * scale) {
                        int localX = (x - charX) / scale;
                        int localY = (y - charY) / scale;
                        
                        if(localX < 5 && localY < 7) {
                            if(font5x7[fontIdx][localX] & (1 << localY)) {
                                pixelByte &= ~(0x80 >> bit);
                            }
                        }
                    }
                }
            }
        }
        
        EPD_SendData(pixelByte);
    }
    
    // 刷新显示
    EPD_dispMass[EPD_dispIndex].show();
    
    Serial.println("✅ 设备码已显示在屏幕上");
}

/* 获取设备ID（基于MAC地址） -----------------------------------------------*/
String getDeviceIdFromMac() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[32];
    
    #if DEVICE_ID_MODE == 1
        // 仅使用MAC地址前6位（前3个字节）
        snprintf(buf, sizeof(buf), "%02X%02X%02X",
                 mac[0], mac[1], mac[2]);
    #elif DEVICE_ID_MODE == 2
        // 仅使用MAC地址后6位（后3个字节）
        snprintf(buf, sizeof(buf), "%02X%02X%02X",
                 mac[3], mac[4], mac[5]);
    #else
        // 使用完整MAC地址（12位，6个字节）
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    #endif
    
    return String(buf);
}

/* 读取本地持久化的claimed状态 ---------------------------------------------*/
bool loadClaimedStatus() {
    preferences.begin(PREF_NAMESPACE, true);  // 只读模式
    bool claimed = preferences.getBool(PREF_KEY_CLAIMED, false);
    preferences.end();
    Serial.printf("📖 读取本地绑定状态: %s\n", claimed ? "已绑定" : "未绑定");
    return claimed;
}

/* 保存本地持久化的claimed状态 ---------------------------------------------*/
void saveClaimedStatus(bool claimed) {
    preferences.begin(PREF_NAMESPACE, false);  // 读写模式
    preferences.putBool(PREF_KEY_CLAIMED, claimed);
    preferences.end();
    Serial.printf("💾 保存本地绑定状态: %s\n", claimed ? "已绑定" : "未绑定");
}

/* 向云端查询设备绑定状态 -------------------------------------------------*/
struct DeviceStatusResponse {
    bool claimed;
    bool hasPairingCode;
    String pairingCode;
    int expiresIn;  // 秒
    String imageUrl;
    int imageVersion;
    bool success;
    String error;
};

DeviceStatusResponse queryDeviceStatus() {
    DeviceStatusResponse result = {false, false, "", 0, "", 0, false, ""};
    
    if (WiFi.status() != WL_CONNECTED) {
        result.error = "WiFi未连接";
        return result;
    }
    
    HTTPClient http;
    String url = "http://" + String(CLOUD_API_HOST) + ":" + String(CLOUD_API_PORT) + "/api/device/status";
    
    Serial.printf("📡 查询绑定状态: %s\n", url.c_str());
    
    http.begin(url);
    http.setTimeout(CLOUD_API_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    
    // 构建请求体
    DynamicJsonDocument doc(256);
    doc["deviceId"] = deviceId;
    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpCode = http.POST(requestBody);
    
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        String response = http.getString();
        Serial.printf("✅ 云端响应: %s\n", response.c_str());
        
        DynamicJsonDocument respDoc(1024);
        DeserializationError error = deserializeJson(respDoc, response);
        
        if (!error) {
            result.success = true;
            result.claimed = respDoc["claimed"].as<bool>();
            
            if (respDoc.containsKey("pairingCode")) {
                result.hasPairingCode = true;
                result.pairingCode = respDoc["pairingCode"].as<String>();
                result.expiresIn = respDoc["expiresIn"].as<int>();
            }
            
            if (respDoc.containsKey("imageUrl")) {
                result.imageUrl = respDoc["imageUrl"].as<String>();
            }
            
            if (respDoc.containsKey("imageVersion")) {
                result.imageVersion = respDoc["imageVersion"].as<int>();
            }
            
            Serial.printf("   绑定状态: %s\n", result.claimed ? "已绑定" : "未绑定");
            if (result.hasPairingCode) {
                Serial.printf("   配对码: %s (有效期: %d秒)\n", result.pairingCode.c_str(), result.expiresIn);
            }
        } else {
            result.error = "JSON解析失败";
            Serial.printf("❌ JSON解析失败: %s\n", error.c_str());
        }
    } else {
        result.error = "HTTP错误: " + String(httpCode);
        Serial.printf("❌ HTTP错误: %d\n", httpCode);
        if (httpCode < 0) {
            Serial.printf("   错误详情: %s\n", http.errorToString(httpCode).c_str());
        }
    }
    
    http.end();
    return result;
}

/* MQTT消息回调函数 --------------------------------------------------------*/
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // 标记设备已激活（收到云端命令）
    if (!deviceActivated) {
        deviceActivated = true;
        Serial.println("\n✅ 设备已激活！");
    }
    
    // 如果收到云端命令，说明设备可能已绑定，更新本地状态
    if (!deviceClaimed) {
        deviceClaimed = true;
        saveClaimedStatus(true);
        Serial.println("✅ 收到云端命令，更新绑定状态为已绑定");
    }
    
    // 立即输出，确认回调被调用
    Serial.println("\n========== MQTT回调被触发 ==========");
    Serial.print("📥 MQTT消息: ");
    Serial.println(topic);
    Serial.printf("消息长度: %d 字节\n", length);
    Serial.printf("剩余内存: %d 字节\n", ESP.getFreeHeap());
    
    // 解析JSON - 增加缓冲区大小以容纳大数据数组
    DynamicJsonDocument doc(8192);  // 增加到8KB
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error) {
        Serial.print("❌ JSON解析失败: ");
        Serial.println(error.c_str());
        Serial.printf("剩余内存: %d 字节\n", ESP.getFreeHeap());
        return;
    }
    
    String cmd = doc["cmd"].as<String>();
    Serial.print("命令: ");
    Serial.println(cmd);
    Serial.printf("剩余内存: %d 字节\n", ESP.getFreeHeap());
    
    // 处理EPD命令
    if (cmd == "EPD") {
        // EPD初始化
        int epdType = doc["type"].as<int>();
        Serial.printf("📱 初始化EPD类型: %d\n", epdType);
        
        EPD_dispIndex = epdType;
        EPD_dispInit();
        Buff__bufInd = 0;
        
        Serial.println("✅ EPD初始化完成");
        Serial.printf("   EPD_dispLoad指针: %p\n", EPD_dispLoad);
        Serial.printf("   EPD型号: %s\n", EPD_dispMass[EPD_dispIndex].title);
        
    } else if (cmd == "SHOW_DEVICE_CODE") {
        // 显示设备码命令（由云端或用户触发）
        Serial.println("📱 收到显示设备码命令");
        displayDeviceCode();
        
    } else if (cmd == "LOAD") {
        // 加载数据（字符串格式，已包含长度后缀）
        String dataStr = doc["data"].as<String>();
        int dataLength = dataStr.length();
        
        Serial.printf("📥 接收数据: %d 字符 ", dataLength);
        
        // 将字符串数据（包括长度后缀）复制到缓冲区
        for (int i = 0; i < dataLength && Buff__bufInd < Buff__SIZE; i++) {
            Buff__bufArr[Buff__bufInd++] = dataStr[i];
        }
        
        // 添加"LOAD"命令后缀（4字符），模拟HTTP方式
        Buff__bufArr[Buff__bufInd++] = 'L';
        Buff__bufArr[Buff__bufInd++] = 'O';
        Buff__bufArr[Buff__bufInd++] = 'A';
        Buff__bufArr[Buff__bufInd++] = 'D';
        
        Serial.printf("(缓冲区: %d/%d) ", Buff__bufInd, Buff__SIZE);
        
        // 立即执行加载（和原版一样，每次LOAD都调用）
        if (EPD_dispLoad != nullptr) {
            Serial.println("→ 执行加载");
            Serial.printf("   缓冲区前20字符: ");
            for(int i = 0; i < 20 && i < Buff__bufInd; i++) {
                Serial.print((char)Buff__bufArr[i]);
            }
            Serial.println("...");
            Serial.printf("   缓冲区后8字符: [");
            for(int i = Buff__bufInd - 8; i < Buff__bufInd && i >= 0; i++) {
                Serial.print((char)Buff__bufArr[i]);
            }
            Serial.println("]");
            Serial.printf("   调用 EPD_dispLoad (指针=%p)...\n", EPD_dispLoad);
            
            EPD_dispLoad();
            
            Serial.println("   ✅ EPD_dispLoad执行完成");
            Buff__bufInd = 0;
        } else {
            Serial.println("❌ EPD_dispLoad未设置！");
        }
        
    } else if (cmd == "NEXT") {
        // 切换通道
        Serial.println("🔄 切换数据通道");
        
        // 先加载当前缓冲区的数据
        if (Buff__bufInd > 0 && EPD_dispLoad != nullptr) {
            EPD_dispLoad();
            Buff__bufInd = 0;
        }
        
        // 切换到下一个颜色通道
        int code = EPD_dispMass[EPD_dispIndex].next;
        
        if (code != -1) {
            Serial.printf("   发送命令: 0x%02X\n", code);
            EPD_SendCommand(code);
            delay(2);
        }
        
        // 设置新的加载函数
        EPD_dispLoad = EPD_dispMass[EPD_dispIndex].chRd;
        Serial.println("✅ 通道切换完成");
        
    } else if (cmd == "SHOW") {
        // 显示
        Serial.println("🎨 刷新显示...");
        
        // 刷新显示
        int epd_array_size = sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]);
        if (EPD_dispIndex >= 0 && EPD_dispIndex < epd_array_size) {
            EPD_dispMass[EPD_dispIndex].show();
            Serial.println("✅ 显示完成");
        } else {
            Serial.println("❌ 无效的EPD索引");
        }
    }
}

/* 连接MQTT ----------------------------------------------------------------*/
void connectMQTT() {
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(8192);  // 增加MQTT缓冲区到8KB
    mqttClient.setKeepAlive(60);     // 设置心跳间隔
    
    Serial.printf("MQTT缓冲区大小: 8192 字节\n");
    
    while (!mqttClient.connected()) {
        Serial.println("正在连接MQTT...");
        String clientId = "dev-" + deviceId;
        
        bool connected;
        if (String(MQTT_USER).length() > 0) {
            connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
        } else {
            connected = mqttClient.connect(clientId.c_str());
        }
        
        if (connected) {
            Serial.println("✅ MQTT已连接");
            
            // 订阅下发主题
            String subTopic = topicDownBase + "/#";
            mqttClient.subscribe(subTopic.c_str(), 1);
            Serial.print("订阅主题: ");
            Serial.println(subTopic);
            
            // 上报在线状态
            String onlineMsg = "{\"online\":1,\"deviceId\":\"" + deviceId + "\"}";
            mqttClient.publish(topicUpStatus.c_str(), onlineMsg.c_str(), true);
            
        } else {
            Serial.print("❌ 连接失败, rc=");
            Serial.println(mqttClient.state());
            delay(2000);
        }
    }
}

/* 上报设备状态 ------------------------------------------------------------*/
void reportStatus() {
    int rssi = WiFi.RSSI();
    IPAddress ip = WiFi.localIP();
    
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"deviceId\":\"%s\",\"rssi\":%d,\"ip\":\"%s\",\"uptime_ms\":%lu,\"freeHeap\":%d}",
             deviceId.c_str(), rssi, ip.toString().c_str(), millis(), ESP.getFreeHeap());
    
    mqttClient.publish(topicUpStatus.c_str(), buf, false);
    Serial.println("📤 状态已上报");
}

/* MQTT模式初始化 ----------------------------------------------------------*/
void MQTT__setup() {
    // 设置默认屏幕型号：7.5" B V2（索引23）
    EPD_dispIndex = 23;
    
    // 获取完整MAC地址用于显示
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char fullMac[32];
    snprintf(fullMac, sizeof(fullMac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // 生成设备ID
    deviceId = getDeviceIdFromMac();
    topicDownBase = "dev/" + deviceId + "/down";
    topicUpStatus = "dev/" + deviceId + "/up/status";
    
    // 读取本地持久化的绑定状态
    deviceClaimed = loadClaimedStatus();
    
    deviceActivated = false;
    deviceStartupTime = millis();
    deviceCodeShown = false;
    
    Serial.println("\n========================================");
    Serial.println("  MQTT云端控制模式");
    Serial.println("========================================");
    Serial.print("完整MAC地址: ");
    Serial.println(fullMac);
    
    #if DEVICE_ID_MODE == 1
        Serial.println("设备码模式: 前6位");
    #elif DEVICE_ID_MODE == 2
        Serial.println("设备码模式: 后6位");
    #else
        Serial.println("设备码模式: 完整12位");
    #endif
    
    Serial.print("⭐ 设备码: ");
    Serial.println(deviceId);
    Serial.print("MQTT服务器: ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);
    Serial.println("========================================");
    Serial.printf("📋 本地绑定状态: %s\n", deviceClaimed ? "已绑定" : "未绑定");
    
    // 如果本地已绑定，优先保持当前画面，不显示设备码
    if (deviceClaimed) {
        Serial.println("✅ 设备已绑定，不显示设备码");
        Serial.println("   将向云端确认绑定状态...");
        Serial.println("   如果云端不可达，继续显示缓存图片");
    } else {
        Serial.println("🔍 设备未绑定，将查询云端状态...");
        Serial.println("   如果云端显示未绑定，将显示配对码");
    }
    Serial.println("========================================\n");
    
    connectMQTT();
}

/* MQTT模式主循环 ----------------------------------------------------------*/
void MQTT__loop() {
    // 保持MQTT连接
    if (!mqttClient.connected()) {
        connectMQTT();
    }
    mqttClient.loop();
    
    unsigned long now = millis();
    
    // 启动后查询云端绑定状态（仅执行一次）
    static bool statusQueried = false;
    if (!statusQueried && (now - deviceStartupTime >= 2000)) {  // 启动后2秒查询
        statusQueried = true;
        
        Serial.println("\n========================================");
        Serial.println("📡 查询云端绑定状态...");
        Serial.println("========================================");
        
        DeviceStatusResponse status = queryDeviceStatus();
        
        if (status.success) {
            // 云端查询成功
            if (status.claimed) {
                // 云端显示已绑定
                if (!deviceClaimed) {
                    // 本地未绑定但云端已绑定，更新本地状态
                    deviceClaimed = true;
                    saveClaimedStatus(true);
                    Serial.println("✅ 云端显示已绑定，更新本地状态");
                }
                Serial.println("✅ 设备已绑定，不显示设备码");
                deviceActivated = true;  // 标记为已激活，避免显示设备码
            } else {
                // 云端显示未绑定
                if (deviceClaimed) {
                    // 本地已绑定但云端未绑定，可能是解绑了，更新本地状态
                    deviceClaimed = false;
                    saveClaimedStatus(false);
                    Serial.println("⚠️  云端显示未绑定，更新本地状态");
                }
                
                // 如果云端返回了配对码，使用云端配对码；否则使用设备ID
                if (status.hasPairingCode && status.pairingCode.length() > 0) {
                    Serial.printf("📱 配对码: %s (有效期: %d秒)\n", 
                                 status.pairingCode.c_str(), status.expiresIn);
                    // 可以在这里显示配对码，但当前实现仍显示设备ID
                }
                
                // 延迟显示设备码，给MQTT消息一些时间
                if (!deviceCodeShown) {
                    deviceCodeShown = true;
                    Serial.println("\n========================================");
                    Serial.println("📱 设备未绑定，显示设备码...");
                    Serial.println("========================================");
                    Serial.println("\n请按以下步骤绑定设备：");
                    Serial.println("1. 查看屏幕上显示的设备码");
                    Serial.printf("2. 访问网页: http://%s:%d\n", CLOUD_API_HOST, CLOUD_API_PORT);
                    Serial.print("3. 输入设备码: ");
                    Serial.println(deviceId);
                    Serial.println("4. 点击[绑定设备]");
                    Serial.println("5. 选择设备，上传图片\n");
                    
                    displayDeviceCode();
                }
            }
        } else {
            // 云端查询失败（离线或网络问题）
            Serial.printf("⚠️  云端查询失败: %s\n", status.error.c_str());
            
            if (deviceClaimed) {
                // 本地已绑定但云端不可达，不显示设备码，继续显示缓存图片
                Serial.println("✅ 本地已绑定，云端不可达时不显示设备码");
                deviceActivated = true;  // 标记为已激活
            } else {
                // 本地未绑定且云端不可达，在合理超时后显示配对码
                Serial.println("⏳ 等待网络恢复或超时后显示配对码...");
                // 延迟显示，给网络一些恢复时间
                if (!deviceCodeShown && (now - deviceStartupTime >= STARTUP_WAIT_MS + 3000)) {
                    deviceCodeShown = true;
                    Serial.println("\n========================================");
                    Serial.println("⚠️  云端不可达，显示设备码...");
                    Serial.println("========================================");
                    Serial.println("\n请按以下步骤绑定设备：");
                    Serial.println("1. 查看屏幕上显示的设备码");
                    Serial.printf("2. 访问网页: http://%s:%d\n", CLOUD_API_HOST, CLOUD_API_PORT);
                    Serial.print("3. 输入设备码: ");
                    Serial.println(deviceId);
                    Serial.println("4. 点击[绑定设备]");
                    Serial.println("5. 选择设备，上传图片\n");
                    
                    displayDeviceCode();
                }
            }
        }
    }
    
    // 如果本地未绑定且未显示设备码，在超时后显示
    if (!deviceClaimed && !deviceCodeShown && !deviceActivated && 
        (now - deviceStartupTime >= STARTUP_WAIT_MS)) {
        deviceCodeShown = true;
        
        Serial.println("\n========================================");
        Serial.println("📱 显示设备码到屏幕...");
        Serial.println("========================================");
        Serial.println("\n请按以下步骤绑定设备：");
        Serial.println("1. 查看屏幕上显示的设备码");
        Serial.printf("2. 访问网页: http://%s:%d\n", CLOUD_API_HOST, CLOUD_API_PORT);
        Serial.print("3. 输入设备码: ");
        Serial.println(deviceId);
        Serial.println("4. 点击[绑定设备]");
        Serial.println("5. 选择设备，上传图片\n");
        
        displayDeviceCode();
    }
    
    // 如果未激活，定期提醒（每60秒）
    static unsigned long lastReminderMs = 0;
    if (!deviceActivated && deviceCodeShown && (now - lastReminderMs >= 60000)) {
        lastReminderMs = now;
        Serial.println("\n⏳ 等待绑定设备...");
        Serial.print("设备码: ");
        Serial.println(deviceId);
        Serial.printf("网页地址: http://%s:%d\n\n", CLOUD_API_HOST, CLOUD_API_PORT);
    }
    
    // 定期上报状态，并在上报后检查绑定状态更新
    static unsigned long lastStatusCheckMs = 0;
    if (now - lastReportMs >= REPORT_INTERVAL_MS) {
        lastReportMs = now;
        reportStatus();
        
        // 每5次状态上报（约2.5分钟）检查一次绑定状态
        if (now - lastStatusCheckMs >= 150000) {
            lastStatusCheckMs = now;
            if (!deviceClaimed) {
                Serial.println("🔄 定期检查绑定状态...");
                DeviceStatusResponse status = queryDeviceStatus();
                if (status.success && status.claimed) {
                    deviceClaimed = true;
                    saveClaimedStatus(true);
                    deviceActivated = true;
                    Serial.println("✅ 检测到设备已绑定！");
                }
            }
        }
    }
}

#endif // MQTT_CONFIG_H
