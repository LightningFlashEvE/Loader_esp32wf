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
#include <SPIFFS.h>
#include <FS.h>
#include "buff.h"
#include "epd.h"
#include "EPD_7in3e.h"  // 官方Demo驱动（用于displayDeviceCode）
#include "GUI_Paint.h"  // GUI绘制库
#include "fonts.h"      // 字库

/* WiFi配置 ------------------------------------------------------------------*/
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

// 全局图像缓冲区（用于显示设备码，避免重复分配）
// 使用半尺寸缓冲区（400x240，48KB），足够显示设备码
#define GLOBAL_IMAGE_BUFFER_WIDTH  400
#define GLOBAL_IMAGE_BUFFER_HEIGHT 240
#define GLOBAL_IMAGE_BUFFER_PACKED_WIDTH  ((GLOBAL_IMAGE_BUFFER_WIDTH + 1) / 2)  // 200
#define GLOBAL_IMAGE_BUFFER_SIZE (GLOBAL_IMAGE_BUFFER_PACKED_WIDTH * GLOBAL_IMAGE_BUFFER_HEIGHT)  // 200 * 240 = 48000
UBYTE globalImageBuffer[GLOBAL_IMAGE_BUFFER_SIZE];  // 静态分配，不占用堆内存

// 设备激活状态
bool deviceActivated = false;
unsigned long deviceStartupTime = 0;
const unsigned long STARTUP_WAIT_MS = 5000;  // 启动后等待5秒
bool deviceCodeShown = false;

// 设备绑定状态（本地持久化）
bool deviceClaimed = false;
const char* PREF_NAMESPACE = "device";
const char* PREF_KEY_CLAIMED = "claimed";

// Flash临时存储配置（用于接收图像数据，避免内存不足）
#define FLASH_TEMP_FILE "/temp_image.bin"  // 临时文件路径
File flashTempFile;  // Flash临时文件句柄
bool flashTempFileOpen = false;  // 文件是否已打开
int flashTempFileSize = 0;  // 已写入的数据大小

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
        // 默认使用 7.3" E6 屏（唯一型号，索引 0）
        EPD_dispIndex = 0;
        Serial.println("⚠️  使用默认屏幕型号: 7.3\" E6 (index=0)");
    }
    
    EPD_dispInit();
    
    // 获取当前屏幕的分辨率
    int width, height;
    
    // 现在只保留 7.3" E6，一律按 800x480 处理（4bit 颜色）
    width = 800;
    height = 480;
    
    Serial.printf("屏幕分辨率: %dx%d\n", width, height);
    
    // 使用官方Demo驱动：初始化（按照官方Demo流程）
    EPD_7IN3E_Init();
    
    // 清屏为白色（Clear内部会刷新显示）
    EPD_7IN3E_Clear(EPD_7IN3E_WHITE);
    delay(1000);  // 等待清屏完成，参考官方Demo
    
    // 重新初始化准备写入新图像（Clear后需要重新Init才能写入）
    EPD_7IN3E_Init();

    // 使用GUI_Paint库绘制设备码（使用官方字库）
    String code = deviceId;
    
    // 使用全局静态缓冲区（半尺寸：400x240，48KB），避免动态分配
    // 这个缓冲区是静态分配的，不占用堆内存，可以安全复用
    int paintWidth = GLOBAL_IMAGE_BUFFER_WIDTH;   // 400
    int paintHeight = GLOBAL_IMAGE_BUFFER_HEIGHT; // 240
    int halfPackedWidth = GLOBAL_IMAGE_BUFFER_PACKED_WIDTH;  // 200
    UBYTE *imageBuffer = globalImageBuffer;  // 使用全局静态缓冲区
    
    Serial.printf("使用全局静态缓冲区: %d 字节 (width=%d, height=%d, packedWidth=%d)\n", 
                  GLOBAL_IMAGE_BUFFER_SIZE, paintWidth, paintHeight, halfPackedWidth);
    Serial.printf("设备码: %s\n", code.c_str());
    
    // 初始化GUI_Paint（使用半尺寸，scale=6会自动放大到全屏）
    Paint_NewImage(imageBuffer, paintWidth, paintHeight, 0, EPD_7IN3E_WHITE);
    Paint_SetScale(6);
    Paint_SelectImage(imageBuffer);
    Paint_Clear(EPD_7IN3E_WHITE);
    
    // 使用Font24计算文字位置（在paint坐标系中）
    int textWidth = code.length() * 12;  // Font24每个字符宽度约12像素
    int textHeight = 24;                // Font24高度24像素
    int startX = (paintWidth - textWidth) / 2;
    int startY = (paintHeight - textHeight) / 2;
    if (startX < 0) startX = 10;
    if (startY < 0) startY = 10;
    
    Serial.printf("文字位置: (%d, %d), 字体: Font24\n", startX, startY);
    
    // 使用Font24绘制文字（蓝色字，白底）
    Paint_DrawString_EN(startX, startY, code.c_str(), &Font24, EPD_7IN3E_WHITE, EPD_7IN3E_BLUE);
    
    // 将半尺寸缓冲区转换为全尺寸并显示
    // 尝试分配全尺寸转换缓冲区
    int fullPackedWidth = (width + 1) / 2;  // 400
    UBYTE *fullImageBuffer = (UBYTE *)malloc(fullPackedWidth * height);
    
    if (fullImageBuffer) {
        // 转换成功：将半尺寸数据放大到全尺寸
        Serial.println("📺 转换缓冲区到全尺寸（放大2倍）...");
        for (int y = 0; y < paintHeight; y++) {
            for (int sy = 0; sy < 2; sy++) {
                int fullY = y * 2 + sy;
                if (fullY >= height) break;
                for (int x = 0; x < halfPackedWidth; x++) {
                    UBYTE pixelPair = imageBuffer[y * halfPackedWidth + x];
                    // 水平放大：每个字节复制2次
                    for (int sx = 0; sx < 2; sx++) {
                        int fullX = x * 2 + sx;
                        if (fullX < fullPackedWidth) {
                            fullImageBuffer[fullY * fullPackedWidth + fullX] = pixelPair;
                        }
                    }
                }
            }
        }
        Serial.println("📺 调用 EPD_7IN3E_Display...");
        EPD_7IN3E_Display(fullImageBuffer);
        Serial.println("✅ Display调用完成");
        free(fullImageBuffer);
    } else {
        // 转换失败：使用DisplayPart直接显示（不放大，但能显示）
        Serial.println("⚠️  全尺寸转换缓冲区分配失败，使用DisplayPart（小尺寸显示）...");
        UWORD xstart = (width - paintWidth) / 2;
        UWORD ystart = (height - paintHeight) / 2;
        Serial.printf("DisplayPart参数: xstart=%d, ystart=%d, width=%d, height=%d\n", 
                      xstart, ystart, paintWidth, paintHeight);
        EPD_7IN3E_DisplayPart(imageBuffer, xstart, ystart, paintWidth, paintHeight);
        Serial.println("✅ DisplayPart调用完成（小尺寸显示，居中）");
    }
    
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

/* Flash存储辅助函数前向声明（在使用前声明） ------------------------------*/
void closeFlashTempFile();
void clearFlashTempFile();

/* MQTT消息回调函数 --------------------------------------------------------*/
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // 立即输出，确认回调被触发（在最开始就输出，确保能看到）
    Serial.println("\n\n========================================");
    Serial.println("🔔 MQTT回调函数被调用！");
    Serial.println("========================================");
    Serial.print("📥 主题: ");
    Serial.println(topic);
    Serial.printf("📏 消息长度: %d 字节 (%.2f KB)\n", length, length / 1024.0);
    Serial.printf("💾 剩余内存: %d 字节\n", ESP.getFreeHeap());
    
    // 标记设备已激活（收到云端命令）
    if (!deviceActivated) {
        deviceActivated = true;
        Serial.println("✅ 设备已激活！");
    }
    
    // 如果收到云端命令，说明设备可能已绑定，更新本地状态
    if (!deviceClaimed) {
        deviceClaimed = true;
        saveClaimedStatus(true);
        Serial.println("✅ 收到云端命令，更新绑定状态为已绑定");
    }
    
    // 检查消息是否可能被截断（MQTT缓冲区限制）
    if (length >= 64 * 1024 - 100) {  // 接近64KB缓冲区
        Serial.println("⚠️  警告：消息大小接近MQTT缓冲区限制，可能被截断！");
    }
    
    // 输出payload的前100个字符（用于调试）
    if (length > 0) {
        Serial.print("📄 消息内容预览（前100字符）: ");
        int previewLen = (length > 100) ? 100 : length;
        for (unsigned int i = 0; i < previewLen; i++) {
            char c = (char)payload[i];
            if (c >= 32 && c < 127) {
                Serial.print(c);
            } else {
                Serial.print('.');
            }
        }
        Serial.println();
    }
    
    // 解析JSON
    Serial.println("📋 开始解析JSON...");
    DynamicJsonDocument doc(2048);  // 2KB（足够解析命令）
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error) {
        Serial.print("❌ JSON解析失败: ");
        Serial.println(error.c_str());
        Serial.printf("   错误代码: %d\n", error.code());
        Serial.printf("   剩余内存: %d 字节\n", ESP.getFreeHeap());
        Serial.println("========================================\n");
        return;
    }
    
    Serial.println("✅ JSON解析成功");
    
    if (!doc.containsKey("cmd")) {
        Serial.println("❌ JSON中缺少cmd字段");
        Serial.println("========================================\n");
        return;
    }
    
    String cmd = doc["cmd"].as<String>();
    Serial.print("📌 命令类型: ");
    Serial.println(cmd);
    Serial.printf("💾 剩余内存: %d 字节\n", ESP.getFreeHeap());
    
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
        // 加载数据（字符串格式：'a'-'p'字符，每两个字符代表一个字节）
        Serial.println("📥 收到LOAD命令");
        
        if (!doc.containsKey("data")) {
            Serial.println("❌ LOAD命令缺少data字段");
            return;
        }
        
        String dataStr = doc["data"].as<String>();
        int dataLength = dataStr.length();
        
        Serial.printf("📥 接收数据: %d 字符\n", dataLength);
        Serial.printf("   剩余内存: %d 字节\n", ESP.getFreeHeap());
        
        // 打开Flash临时文件（第一次打开时清空，后续追加）
        if (!flashTempFileOpen) {
            // 清除旧的临时文件
            if (SPIFFS.exists(FLASH_TEMP_FILE)) {
                SPIFFS.remove(FLASH_TEMP_FILE);
                Serial.println("🗑️  已清除旧的临时文件");
            }
            flashTempFile = SPIFFS.open(FLASH_TEMP_FILE, "w");  // 写入模式，创建新文件
            if (!flashTempFile) {
                Serial.println("❌ 无法创建Flash临时文件");
                Serial.printf("   SPIFFS可用空间: %d 字节\n", SPIFFS.totalBytes() - SPIFFS.usedBytes());
                return;
            }
            flashTempFileOpen = true;
            flashTempFileSize = 0;
            Serial.println("📁 已创建Flash临时文件");
        }
        
        // 将字符串数据直接写入Flash（不经过RAM缓冲区）
        int written = flashTempFile.print(dataStr);
        flashTempFile.flush();  // 确保数据写入Flash
        flashTempFileSize += written;
        
        Serial.printf("✅ 已写入Flash: %d 字节 (总大小: %d 字节)\n", written, flashTempFileSize);
        Serial.printf("   剩余内存: %d 字节\n", ESP.getFreeHeap());
        
    } else if (cmd == "DOWNLOAD") {
        // HTTP下载命令：从URL下载图像数据并保存到Flash
        Serial.println("\n========== 收到DOWNLOAD命令 ==========");
        Serial.println("📥 HTTP下载模式");
        
        if (!doc.containsKey("url")) {
            Serial.println("❌ DOWNLOAD命令缺少url字段");
            Serial.println("   请检查后端是否正确发送了url");
            return;
        }
        
        String downloadUrl = doc["url"].as<String>();
        Serial.printf("   下载URL: %s\n", downloadUrl.c_str());
        Serial.printf("   URL长度: %d 字符\n", downloadUrl.length());
        Serial.printf("   剩余内存: %d 字节\n", ESP.getFreeHeap());
        Serial.println("   开始HTTP下载...");
        
        // 打开Flash临时文件
        if (SPIFFS.exists(FLASH_TEMP_FILE)) {
            SPIFFS.remove(FLASH_TEMP_FILE);
        }
        flashTempFile = SPIFFS.open(FLASH_TEMP_FILE, "w");
        if (!flashTempFile) {
            Serial.println("❌ 无法创建Flash临时文件");
            return;
        }
        flashTempFileOpen = true;
        flashTempFileSize = 0;
        
        // 使用HTTPClient下载
        Serial.println("   初始化HTTP客户端...");
        HTTPClient http;
        bool beginResult = http.begin(downloadUrl);
        if (!beginResult) {
            Serial.println("❌ HTTP begin失败！URL可能无效");
            flashTempFile.close();
            flashTempFileOpen = false;
            if (SPIFFS.exists(FLASH_TEMP_FILE)) {
                SPIFFS.remove(FLASH_TEMP_FILE);
            }
            return;
        }
        
        http.setTimeout(30000);  // 30秒超时
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        
        Serial.println("   发送HTTP GET请求...");
        int httpCode = http.GET();
        Serial.printf("   HTTP状态码: %d\n", httpCode);
        
        if (httpCode <= 0) {
            Serial.printf("❌ HTTP请求失败: %d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
            http.end();
            flashTempFile.close();
            flashTempFileOpen = false;
            if (SPIFFS.exists(FLASH_TEMP_FILE)) {
                SPIFFS.remove(FLASH_TEMP_FILE);
            }
            return;
        }
        
        if (httpCode == HTTP_CODE_OK) {
            int contentLength = http.getSize();
            Serial.printf("   内容长度: %d 字节 (%.2f KB)\n", contentLength, contentLength / 1024.0);
            
            // 获取数据流
            WiFiClient *stream = http.getStreamPtr();
            uint8_t buffer[512];  // 512字节缓冲区，分块读取
            int totalRead = 0;
            unsigned long startTime = millis();
            const unsigned long DOWNLOAD_TIMEOUT = 60000;  // 60秒超时
            int noDataCount = 0;
            const int MAX_NO_DATA_COUNT = 100;  // 最多等待1秒（100 * 10ms）
            
            while (http.connected() && (contentLength > 0 || contentLength == -1)) {
                // 检查超时
                if (millis() - startTime > DOWNLOAD_TIMEOUT) {
                    Serial.println("❌ 下载超时！");
                    break;
                }
                
                size_t available = stream->available();
                if (available) {
                    noDataCount = 0;  // 重置无数据计数
                    int bytesToRead = (available > sizeof(buffer)) ? sizeof(buffer) : available;
                    int bytesRead = stream->readBytes(buffer, bytesToRead);
                    
                    // 直接写入Flash（不经过RAM）
                    flashTempFile.write(buffer, bytesRead);
                    flashTempFile.flush();
                    flashTempFileSize += bytesRead;
                    totalRead += bytesRead;
                    
                    if (contentLength > 0) {
                        contentLength -= bytesRead;
                    }
                    
                    // 每32KB输出一次进度
                    if (totalRead % 32768 == 0) {
                        Serial.printf("   已下载: %d 字节 (%.2f KB)\n", totalRead, totalRead / 1024.0);
                    }
                } else {
                    noDataCount++;
                    if (noDataCount > MAX_NO_DATA_COUNT) {
                        Serial.println("⚠️  长时间无数据，可能下载完成或连接断开");
                        break;
                    }
                    delay(10);
                }
            }
            
            flashTempFile.close();
            flashTempFileOpen = false;
            
            Serial.printf("✅ 下载完成: %d 字节 (%.2f KB)\n", flashTempFileSize, flashTempFileSize / 1024.0);
            Serial.println("   数据已保存到Flash，可以使用SHOW命令显示");
        } else {
            Serial.printf("❌ HTTP下载失败: 状态码 %d\n", httpCode);
            if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED) {
                Serial.println("   错误：连接被拒绝，请检查服务器是否运行");
            } else if (httpCode == HTTPC_ERROR_CONNECTION_LOST) {
                Serial.println("   错误：连接丢失");
            } else if (httpCode == HTTPC_ERROR_NO_HTTP_SERVER) {
                Serial.println("   错误：找不到HTTP服务器");
            }
            flashTempFile.close();
            flashTempFileOpen = false;
            if (SPIFFS.exists(FLASH_TEMP_FILE)) {
                SPIFFS.remove(FLASH_TEMP_FILE);
            }
        }
        
        http.end();
        Serial.println("========== DOWNLOAD命令处理完成 ==========\n");
        
    } else if (cmd == "SHOW") {
        // 显示命令：从Flash读取数据并显示
        Serial.println("📺 收到显示命令，从Flash读取数据...");
        
        // 关闭写入文件
        closeFlashTempFile();
        
        // 立即执行加载（从Flash读取）
        if (EPD_dispLoad != nullptr) {
            Serial.printf("   调用 EPD_dispLoad (指针=%p)...\n", EPD_dispLoad);
            EPD_dispLoad();
            Serial.println("   ✅ 显示完成");
            
            // 显示完成后，清除Flash临时文件（释放空间，因为墨水屏已显示）
            clearFlashTempFile();
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
    // MQTT缓冲区最大限制：uint16_t 类型，最大 65535 字节（约64KB）
    // 注意：800x480 4bit图像转换为字符串后约384KB，无法通过单条MQTT消息传输
    // 解决方案：使用HTTP直接下载或分批MQTT传输
    mqttClient.setBufferSize(65535);  // 最大64KB缓冲区
    mqttClient.setKeepAlive(60);     // 设置心跳间隔
    
    Serial.printf("MQTT缓冲区大小: 64KB (最大限制)\n");
    Serial.println("⚠️  注意：大图像数据需要通过HTTP下载或分批MQTT传输");
    
    while (!mqttClient.connected()) {
        Serial.println("正在连接MQTT...");
        Serial.printf("   MQTT服务器: %s:%d\n", MQTT_HOST, MQTT_PORT);
        String clientId = "dev-" + deviceId;
        Serial.printf("   客户端ID: %s\n", clientId.c_str());
        
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

/* Flash存储辅助函数 ------------------------------------------------------*/
bool initFlashStorage() {
    Serial.println("📁 初始化SPIFFS文件系统...");
    
    // 尝试挂载SPIFFS（先尝试不格式化）
    if (!SPIFFS.begin(false)) {
        Serial.println("⚠️  SPIFFS挂载失败，尝试格式化...");
        
        // 如果挂载失败，尝试格式化
        if (!SPIFFS.format()) {
            Serial.println("❌ SPIFFS格式化失败");
            Serial.println("   可能原因：分区表未配置SPIFFS分区");
            Serial.println("   解决方案：");
            Serial.println("   1. 在Arduino IDE中选择包含SPIFFS的分区方案，或");
            Serial.println("   2. 使用项目根目录的 partitions.csv 文件");
            Serial.println("   3. 在Arduino IDE中：工具 -> Partition Scheme -> 选择包含SPIFFS的方案");
            return false;
        }
        
        Serial.println("✅ SPIFFS格式化完成，重新挂载...");
        
        // 格式化后重新挂载
        if (!SPIFFS.begin(false)) {
            Serial.println("❌ SPIFFS重新挂载失败");
            Serial.println("   错误代码可能表示分区不存在");
            return false;
        }
    }
    
    Serial.println("✅ SPIFFS初始化成功");
    
    // 显示SPIFFS信息
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    Serial.printf("   SPIFFS总大小: %d 字节 (%.2f KB)\n", totalBytes, totalBytes / 1024.0);
    Serial.printf("   已使用: %d 字节 (%.2f KB)\n", usedBytes, usedBytes / 1024.0);
    Serial.printf("   可用: %d 字节 (%.2f KB)\n", totalBytes - usedBytes, (totalBytes - usedBytes) / 1024.0);
    
    // 清除旧的临时文件
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
        Serial.println("🗑️  已清除旧的临时文件");
    }
    
    flashTempFileOpen = false;
    flashTempFileSize = 0;
    return true;
}

void closeFlashTempFile() {
    if (flashTempFileOpen && flashTempFile) {
        flashTempFile.close();
        flashTempFileOpen = false;
        Serial.printf("📁 Flash文件已关闭，总大小: %d 字节\n", flashTempFileSize);
    }
}

void clearFlashTempFile() {
    closeFlashTempFile();
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
        Serial.println("🗑️  Flash临时文件已清除");
    }
    flashTempFileSize = 0;
}

/* MQTT模式初始化 ----------------------------------------------------------*/
void MQTT__setup() {
    // 初始化Flash存储（SPIFFS）
    initFlashStorage();
    
    // 设置默认屏幕型号：7.3" E6（唯一型号，索引 0）
    EPD_dispIndex = 0;
    
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
    static unsigned long lastStatusCheck = 0;
    if (!mqttClient.connected()) {
        Serial.println("⚠️  MQTT连接断开，尝试重连...");
        connectMQTT();
    }
    
    // 每30秒输出一次状态（用于确认程序在运行）
    unsigned long now = millis();
    if (now - lastStatusCheck > 30000) {
        lastStatusCheck = now;
        Serial.printf("[心跳] MQTT连接: %s, 剩余内存: %d 字节\n", 
                      mqttClient.connected() ? "已连接" : "未连接", 
                      ESP.getFreeHeap());
    }
    
    mqttClient.loop();
    
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
