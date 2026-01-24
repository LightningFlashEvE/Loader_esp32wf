/**
 ******************************************************************************
 * @file    http_update.h
 * @author  Modified for Deep-sleep + HTTP Pull Architecture
 * @version V3.0.0
 * @date    2026-01-24
 * @brief   Deep-sleep + HTTP 拉取更新架构
 *          设备绝大多数时间处于 Deep-sleep（µA 级），
 *          只有按键或定时醒来后才联网拉取更新图片
 ******************************************************************************
 */

#ifndef HTTP_UPDATE_H
#define HTTP_UPDATE_H

#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <FS.h>
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "buff.h"
#include "epd.h"
#include "EPD_7in3e.h"
#include "GUI_Paint.h"
#include "fonts.h"

/* ============================================================================
 *                               配置参数
 * ============================================================================ */

/* 云端API配置 */
#define CLOUD_API_HOST "8.135.238.216"
#define CLOUD_API_PORT 5000
#define CLOUD_API_TIMEOUT_MS 10000  // HTTP请求超时时间（10秒）
#define CLOUD_DOWNLOAD_TIMEOUT_MS 60000  // 下载超时时间（60秒）

/* 设备ID配置 */
// 选择设备ID生成方式：
// 0 = 使用完整MAC地址 (12位，例如: 112233445566)
// 1 = 仅使用MAC地址前6位 (例如: 112233)
// 2 = 仅使用MAC地址后6位 (例如: 445566)
#define DEVICE_ID_MODE 2

/* Deep-sleep 配置 */
#define WAKEUP_GPIO GPIO_NUM_0  // GPIO0 按键唤醒（按键接地，低电平唤醒）
#define DEEP_SLEEP_INTERVAL_HOURS 12  // 定时唤醒间隔（小时）
#define DEEP_SLEEP_INTERVAL_US (DEEP_SLEEP_INTERVAL_HOURS * 60ULL * 60ULL * 1000000ULL)
// 避免“按键仍按下/引脚为低”导致刚入睡就立刻被再次唤醒
#define WAKEUP_RELEASE_WAIT_MS 2500

/* Flash临时存储配置 */
#define FLASH_TEMP_FILE "/temp_image.bin"

/* NVS 配置 */
#define PREF_NAMESPACE "device"
#define PREF_KEY_CLAIMED "claimed"
#define PREF_KEY_IMG_VER "imgVer"

/* 全局图像缓冲区（用于显示设备码） */
#define GLOBAL_IMAGE_BUFFER_WIDTH  400
#define GLOBAL_IMAGE_BUFFER_HEIGHT 240
#define GLOBAL_IMAGE_BUFFER_PACKED_WIDTH  ((GLOBAL_IMAGE_BUFFER_WIDTH + 1) / 2)
#define GLOBAL_IMAGE_BUFFER_SIZE (GLOBAL_IMAGE_BUFFER_PACKED_WIDTH * GLOBAL_IMAGE_BUFFER_HEIGHT)
UBYTE globalImageBuffer[GLOBAL_IMAGE_BUFFER_SIZE];

/* ============================================================================
 *                               全局变量
 * ============================================================================ */

extern Preferences preferences;  // NVS持久化存储（在Loader_esp32wf.ino中定义）

String deviceId;
bool deviceClaimed = false;
int localImageVersion = 0;

/* Flash临时文件 */
File flashTempFile;
bool flashTempFileOpen = false;
int flashTempFileSize = 0;

/* ============================================================================
 *                          本次唤醒的“一次性”状态机
 * 目标：
 * - 每次唤醒只做一次 status 检查（避免 loop 重复检查）
 * - 仅当需要更新时才在 loop 中执行下载+刷新
 * - Deep-sleep 进入流程幂等化（避免异常情况下重复执行）
 * ============================================================================ */

static bool g_statusChecked = false;          // 本次唤醒是否已完成 status 判定
static bool g_updateNeeded = false;           // 本次唤醒是否需要更新
static bool g_updateAttempted = false;        // 本次唤醒是否已尝试更新（避免重复下载）
static bool g_shouldEnterDeepSleep = false;   // 本次唤醒是否应立即回睡
static bool g_deepSleepRequested = false;     // 防止重复执行 deep-sleep 进入流程
static int g_targetImageVersion = 0;          // 需要更新到的版本
static String g_targetImageUrl = "";          // 需要下载的 URL

/* ============================================================================
 *                            辅助函数：设备ID
 * ============================================================================ */

/**
 * 获取设备ID（基于MAC地址）
 */
inline String getDeviceIdFromMac() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[32];
    
    #if DEVICE_ID_MODE == 1
        snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[0], mac[1], mac[2]);
    #elif DEVICE_ID_MODE == 2
        snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[3], mac[4], mac[5]);
    #else
        snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    #endif
    
    return String(buf);
}

/* ============================================================================
 *                            辅助函数：NVS 存储
 * ============================================================================ */

/**
 * 读取本地持久化的claimed状态
 */
bool loadClaimedStatus() {
    if (!preferences.begin(PREF_NAMESPACE, true)) {
        preferences.end();
        Serial.println("📖 读取本地绑定状态: 未绑定（首次使用）");
        return false;
    }
    bool claimed = preferences.getBool(PREF_KEY_CLAIMED, false);
    preferences.end();
    Serial.printf("📖 读取本地绑定状态: %s\n", claimed ? "已绑定" : "未绑定");
    return claimed;
}

/**
 * 保存本地持久化的claimed状态
 */
void saveClaimedStatus(bool claimed) {
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        Serial.println("⚠️  NVS命名空间打开失败，无法保存绑定状态");
        return;
    }
    preferences.putBool(PREF_KEY_CLAIMED, claimed);
    preferences.end();
    Serial.printf("💾 保存本地绑定状态: %s\n", claimed ? "已绑定" : "未绑定");
}

/**
 * 读取本地图片版本号
 */
int loadImageVersion() {
    if (!preferences.begin(PREF_NAMESPACE, true)) {
        preferences.end();
        return 0;
    }
    int version = preferences.getInt(PREF_KEY_IMG_VER, 0);
    preferences.end();
    Serial.printf("📖 读取本地图片版本: %d\n", version);
    return version;
}

/**
 * 保存本地图片版本号
 */
void saveImageVersion(int version) {
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        Serial.println("⚠️  NVS命名空间打开失败，无法保存图片版本");
        return;
    }
    preferences.putInt(PREF_KEY_IMG_VER, version);
    preferences.end();
    Serial.printf("💾 保存本地图片版本: %d\n", version);
}

/* ============================================================================
 *                            辅助函数：Flash 存储
 * ============================================================================ */

/**
 * 初始化Flash存储（SPIFFS）
 */
bool initFlashStorage() {
    Serial.println("📁 初始化SPIFFS文件系统...");
    
    if (!SPIFFS.begin(false)) {
        Serial.println("⚠️  SPIFFS挂载失败，尝试格式化...");
        if (!SPIFFS.format()) {
            Serial.println("❌ SPIFFS格式化失败");
            return false;
        }
        if (!SPIFFS.begin(false)) {
            Serial.println("❌ SPIFFS重新挂载失败");
            return false;
        }
    }
    
    Serial.println("✅ SPIFFS初始化成功");
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    Serial.printf("   总大小: %.2f KB, 已使用: %.2f KB, 可用: %.2f KB\n", 
                  totalBytes / 1024.0, usedBytes / 1024.0, (totalBytes - usedBytes) / 1024.0);
    
    // 清除旧的临时文件
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
        Serial.println("🗑️  已清除旧的临时文件");
    }
    
    flashTempFileOpen = false;
    flashTempFileSize = 0;
    return true;
}

/**
 * 关闭Flash临时文件
 */
void closeFlashTempFile() {
    if (flashTempFileOpen && flashTempFile) {
        flashTempFile.close();
        flashTempFileOpen = false;
        Serial.printf("📁 Flash文件已关闭，总大小: %d 字节\n", flashTempFileSize);
    }
}

/**
 * 清除Flash临时文件
 */
void clearFlashTempFile() {
    closeFlashTempFile();
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
        Serial.println("🗑️  Flash临时文件已清除");
    }
    flashTempFileSize = 0;
}

/* ============================================================================
 *                            辅助函数：显示设备码
 * ============================================================================ */

/**
 * 在屏幕上显示设备码（使用大号数字）
 */
void displayDeviceCode() {
    Serial.println("📱 开始显示设备码...");
    Serial.print("⭐ 设备码: ");
    Serial.println(deviceId);
    
    // 默认使用 7.3" E6 屏
    if (EPD_dispIndex < 0 || EPD_dispIndex >= (sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]))) {
        EPD_dispIndex = 0;
    }
    
    EPD_dispInit();
    
    int width = 800;
    int height = 480;
    
    EPD_7IN3E_Init();
    
    String code = deviceId;
    int paintWidth = GLOBAL_IMAGE_BUFFER_WIDTH;
    int paintHeight = GLOBAL_IMAGE_BUFFER_HEIGHT;
    UBYTE *imageBuffer = globalImageBuffer;
    
    Paint_NewImage(imageBuffer, paintWidth, paintHeight, 0, EPD_7IN3E_WHITE);
    Paint_SetScale(6);
    Paint_SelectImage(imageBuffer);
    Paint_Clear(EPD_7IN3E_WHITE);
    
    // 手动放大字体
    int fontScale = 2;
    int charWidth = Font24.Width * fontScale;
    int charHeight = Font24.Height * fontScale;
    int textWidth = code.length() * charWidth;
    int textHeight = charHeight;
    int startX = (paintWidth - textWidth) / 2;
    int startY = (paintHeight - textHeight) / 2;
    if (startX < 0) startX = 20;
    if (startY < 0) startY = 20;
    
    const char* pStr = code.c_str();
    int charX = startX;
    int charY = startY;
    
    while (*pStr != '\0') {
        char c = *pStr;
        uint32_t Char_Offset = (c - ' ') * Font24.Height * (Font24.Width / 8 + (Font24.Width % 8 ? 1 : 0));
        const unsigned char *ptr = &Font24.table[Char_Offset];
        
        for (int Page = 0; Page < Font24.Height; Page++) {
            for (int Column = 0; Column < Font24.Width; Column++) {
                bool pixelOn = (*ptr & (0x80 >> (Column % 8))) != 0;
                
                for (int sy = 0; sy < fontScale; sy++) {
                    for (int sx = 0; sx < fontScale; sx++) {
                        int px = charX + Column * fontScale + sx;
                        int py = charY + Page * fontScale + sy;
                        if (px < paintWidth && py < paintHeight) {
                            Paint_SetPixel(px, py, pixelOn ? EPD_7IN3E_BLUE : EPD_7IN3E_WHITE);
                        }
                    }
                }
                
                if (Column % 8 == 7) ptr++;
            }
            if (Font24.Width % 8 != 0) ptr++;
        }
        
        charX += charWidth;
        pStr++;
    }
    
    UWORD xstart = (width - paintWidth) / 2;
    UWORD ystart = (height - paintHeight) / 2;
    
    EPD_7IN3E_DisplayPart(imageBuffer, xstart, ystart, paintWidth, paintHeight);
    
    Serial.println("✅ 设备码已显示在屏幕上");
}

/* ============================================================================
 *                            云端API调用
 * ============================================================================ */

/**
 * 设备状态响应结构
 */
struct DeviceStatusResponse {
    bool success;
    bool claimed;
    int imageVersion;
    String imageUrl;
    String error;
};

/**
 * 向云端查询设备状态
 */
DeviceStatusResponse queryDeviceStatus() {
    DeviceStatusResponse result = {false, false, 0, "", ""};
    
    if (WiFi.status() != WL_CONNECTED) {
        result.error = "WiFi未连接";
        return result;
    }
    
    HTTPClient http;
    String url = "http://" + String(CLOUD_API_HOST) + ":" + String(CLOUD_API_PORT) + "/api/device/status";
    
    Serial.printf("📡 查询设备状态: %s\n", url.c_str());
    
    http.begin(url);
    http.setTimeout(CLOUD_API_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<256> doc;
    doc["deviceId"] = deviceId;
    String requestBody;
    serializeJson(doc, requestBody);
    
    int httpCode = http.POST(requestBody);
    
    if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        String response = http.getString();
        Serial.printf("✅ 云端响应: %s\n", response.c_str());
        
        StaticJsonDocument<1024> respDoc;
        DeserializationError error = deserializeJson(respDoc, response);
        
        if (!error) {
            result.success = true;
            result.claimed = respDoc["claimed"].as<bool>();
            
            if (respDoc["imageVersion"].is<int>()) {
                result.imageVersion = respDoc["imageVersion"].as<int>();
            }
            
            if (respDoc["imageUrl"].is<String>()) {
                result.imageUrl = respDoc["imageUrl"].as<String>();
            }
            
            Serial.printf("   绑定状态: %s\n", result.claimed ? "已绑定" : "未绑定");
            Serial.printf("   图片版本: %d\n", result.imageVersion);
            if (result.imageUrl.length() > 0) {
                Serial.printf("   图片URL: %s\n", result.imageUrl.c_str());
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

/**
 * 流式下载图片数据到SPIFFS（不占用大量RAM）
 * @param imageUrl 图片下载URL
 * @return 下载是否成功
 */
bool downloadImageToFlash(const String& imageUrl) {
    Serial.println("\n========== 开始下载图片 ==========");
    Serial.printf("   URL: %s\n", imageUrl.c_str());
    Serial.printf("   剩余内存: %d 字节\n", ESP.getFreeHeap());
    
    // 清除旧文件并创建新文件
    if (SPIFFS.exists(FLASH_TEMP_FILE)) {
        SPIFFS.remove(FLASH_TEMP_FILE);
    }
    
    flashTempFile = SPIFFS.open(FLASH_TEMP_FILE, "w");
    if (!flashTempFile) {
        Serial.println("❌ 无法创建Flash临时文件");
        return false;
    }
    flashTempFileOpen = true;
    flashTempFileSize = 0;
    
    HTTPClient http;
    if (!http.begin(imageUrl)) {
        Serial.println("❌ HTTP begin失败");
        flashTempFile.close();
        flashTempFileOpen = false;
        return false;
    }
    
    http.setTimeout(CLOUD_DOWNLOAD_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    int httpCode = http.GET();
    Serial.printf("   HTTP状态码: %d\n", httpCode);
    
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("❌ HTTP下载失败: %d\n", httpCode);
        http.end();
        flashTempFile.close();
        flashTempFileOpen = false;
        SPIFFS.remove(FLASH_TEMP_FILE);
        return false;
    }
    
    int contentLength = http.getSize();
    Serial.printf("   内容长度: %d 字节 (%.2f KB)\n", contentLength, contentLength / 1024.0);
    
    // 流式下载，分块写入SPIFFS
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[512];  // 512字节缓冲区
    int totalRead = 0;
    unsigned long startTime = millis();
    int noDataCount = 0;
    const int MAX_NO_DATA_COUNT = 100;
    
    while (http.connected() && (contentLength > 0 || contentLength == -1)) {
        if (millis() - startTime > CLOUD_DOWNLOAD_TIMEOUT_MS) {
            Serial.println("❌ 下载超时！");
            break;
        }
        
        size_t available = stream->available();
        if (available) {
            noDataCount = 0;
            int bytesToRead = (available > sizeof(buffer)) ? sizeof(buffer) : available;
            int bytesRead = stream->readBytes(buffer, bytesToRead);
            
            // 直接写入Flash
            flashTempFile.write(buffer, bytesRead);
            flashTempFileSize += bytesRead;
            totalRead += bytesRead;
            
            if (contentLength > 0) {
                contentLength -= bytesRead;
            }
            
            // 每64KB输出一次进度
            if (totalRead % 65536 == 0) {
                Serial.printf("   已下载: %.2f KB\n", totalRead / 1024.0);
            }
        } else {
            noDataCount++;
            if (noDataCount > MAX_NO_DATA_COUNT) {
                break;
            }
            delay(10);
        }
    }
    
    flashTempFile.flush();
    flashTempFile.close();
    flashTempFileOpen = false;
    
    http.end();
    
    // 检查下载结果
    int expectedSize = 384000;  // 800x480 4bit格式 = 384000字符
    Serial.printf("✅ 下载完成: %d 字符 (%.2f KB)\n", flashTempFileSize, flashTempFileSize / 1024.0);
    Serial.printf("   期望大小: %d 字符\n", expectedSize);
    
    if (flashTempFileSize < expectedSize * 0.9) {  // 允许10%误差
        Serial.println("⚠️  警告：下载的数据可能不完整");
    }
    
    Serial.println("========== 下载完成 ==========\n");
    return flashTempFileSize > 0;
}

/**
 * 显示下载的图片（从Flash读取并刷新EPD）
 */
void displayDownloadedImage() {
    Serial.println("📺 开始显示图片...");
    
    if (!SPIFFS.exists(FLASH_TEMP_FILE)) {
        Serial.println("❌ 临时文件不存在");
        return;
    }
    
    // 初始化EPD
    if (EPD_dispIndex < 0 || EPD_dispIndex >= (sizeof(EPD_dispMass) / sizeof(EPD_dispMass[0]))) {
        EPD_dispIndex = 0;
    }
    EPD_dispInit();
    
    // 调用显示函数（从Flash读取）
    if (EPD_dispLoad != nullptr) {
        EPD_dispLoad();
        Serial.println("✅ 图片显示完成");
    } else {
        Serial.println("❌ EPD_dispLoad未设置");
    }
    
    // 清除临时文件
    clearFlashTempFile();
}

/* ============================================================================
 *                            Deep-sleep 管理
 * ============================================================================ */

/**
 * 打印唤醒原因
 */
void printWakeupReason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    Serial.println("\n========================================");
    Serial.print("⏰ 唤醒原因: ");
    
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0:
            Serial.println("外部信号 (RTC_IO) 唤醒");
            break;
        case ESP_SLEEP_WAKEUP_EXT1:
            Serial.println("外部信号 (RTC_CNTL) 唤醒");
            break;
        case ESP_SLEEP_WAKEUP_TIMER:
            Serial.println("定时器唤醒 (每12小时)");
            break;
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
            Serial.println("触摸板唤醒");
            break;
        case ESP_SLEEP_WAKEUP_ULP:
            Serial.println("ULP程序唤醒");
            break;
        case ESP_SLEEP_WAKEUP_GPIO:
            Serial.println("GPIO按键唤醒");
            break;
        default:
            Serial.printf("其他原因 (%d) - 首次启动或复位\n", wakeup_reason);
            break;
    }
    Serial.println("========================================\n");
}

/**
 * 配置Deep-sleep唤醒源并进入睡眠
 */
void enterDeepSleep() {
    // 幂等：如果已经开始准备进入 deep-sleep，避免重复执行关 WiFi/配置唤醒源等耗时动作
    if (g_deepSleepRequested) {
        Serial.flush();
        delay(50);
        esp_deep_sleep_start();
        return;
    }
    g_deepSleepRequested = true;

    Serial.println("\n========================================");
    Serial.println("💤 准备进入Deep-sleep...");
    Serial.println("========================================");
    
    // 1. 关闭WiFi
    Serial.println("   关闭WiFi...");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop();
    delay(100);
    
    // 1.5 处理按键“仍处于低电平”的情况：等待松开再入睡，避免立即再次唤醒
    // 同时启用内部上拉（仍建议外部上拉电阻，避免深睡时上拉状态不稳）
    pinMode((int)WAKEUP_GPIO, INPUT_PULLUP);
    gpio_pullup_en(WAKEUP_GPIO);
    gpio_pulldown_dis(WAKEUP_GPIO);

    if (gpio_get_level(WAKEUP_GPIO) == 0) {
        Serial.println("⚠️  检测到GPIO0仍为低电平（按键可能未松开/无上拉），等待释放...");
        unsigned long startWait = millis();
        while (gpio_get_level(WAKEUP_GPIO) == 0 && (millis() - startWait) < WAKEUP_RELEASE_WAIT_MS) {
            delay(20);
        }
        if (gpio_get_level(WAKEUP_GPIO) == 0) {
            Serial.println("⚠️  等待超时，GPIO0仍为低电平：可能会立刻再次唤醒（请检查硬件上拉/按键）");
        } else {
            Serial.println("✅ GPIO0已恢复高电平，继续进入Deep-sleep");
        }
    }

    // 2. 配置GPIO0按键唤醒（低电平唤醒）
    // ESP32-C3使用esp_deep_sleep_enable_gpio_wakeup
    Serial.println("   配置GPIO0按键唤醒...");
    esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKEUP_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    
    // 3. 配置定时唤醒（12小时）
    Serial.printf("   配置定时唤醒: %d 小时\n", DEEP_SLEEP_INTERVAL_HOURS);
    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_INTERVAL_US);
    
    // 4. 打印信息
    Serial.println("\n✅ Deep-sleep配置完成:");
    Serial.println("   - GPIO0 按键唤醒（低电平）");
    Serial.printf("   - 定时唤醒: %d 小时后\n", DEEP_SLEEP_INTERVAL_HOURS);
    Serial.println("   - 墨水屏将保持当前画面");
    Serial.println("\n💤 进入Deep-sleep...\n");
    Serial.flush();
    delay(100);
    
    // 5. 进入Deep-sleep
    esp_deep_sleep_start();
    
    // 不会执行到这里
}

/* ============================================================================
 *                            主要更新流程（一次性判定 + 条件执行）
 * ============================================================================ */

/**
 * 本次唤醒：执行一次性“是否需要更新”的判定（不在这里下载/刷新）
 * - 只做 status 查询与版本比较
 * - 结果写入 g_updateNeeded/g_target*，供 loop 决策
 */
void prepareUpdateDecisionOnce() {
    Serial.println("\n========================================");
    Serial.println("🔄 开始一次性更新判定（仅检查，不下载）...");
    Serial.println("========================================\n");

    // 防止被重复调用（例如某些异常路径下 setup/loop 误触发）
    if (g_statusChecked) {
        Serial.println("ℹ️ 本次唤醒已完成过更新判定，跳过重复检查");
        return;
    }
    
    // 1. 初始化设备ID
    deviceId = getDeviceIdFromMac();
    Serial.printf("⭐ 设备ID: %s\n", deviceId.c_str());
    
    // 2. 读取本地状态
    deviceClaimed = loadClaimedStatus();
    localImageVersion = loadImageVersion();
    Serial.printf("📋 本地状态: claimed=%s, imageVersion=%d\n", 
                  deviceClaimed ? "是" : "否", localImageVersion);
    
    // 3. 初始化Flash存储
    if (!initFlashStorage()) {
        Serial.println("❌ Flash初始化失败，本次唤醒直接进入Deep-sleep");
        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }
    
    // 4. 设置默认EPD型号
    EPD_dispIndex = 0;
    
    // 5. 基础检查：WiFi 必须已连接（理论上 .ino 已保证，这里兜底）
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️  WiFi未连接，跳过云端查询，直接进入Deep-sleep");
        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }

    // 5. 查询云端状态
    Serial.println("\n📡 查询云端状态...");
    DeviceStatusResponse status = queryDeviceStatus();
    
    if (!status.success) {
        Serial.printf("❌ 云端查询失败: %s\n", status.error.c_str());
        Serial.println("   直接进入Deep-sleep，下次唤醒再试");
        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }
    
    // 6. 处理绑定状态
    if (!status.claimed) {
        Serial.println("\n📱 设备未绑定，显示设备码...");
        
        // 更新本地状态
        if (deviceClaimed) {
            deviceClaimed = false;
            saveClaimedStatus(false);
        }
        
        // 显示设备码
        displayDeviceCode();
        
        Serial.println("✅ 设备码已显示，请通过网页绑定设备");
        Serial.printf("   网页地址: http://%s:%d\n", CLOUD_API_HOST, CLOUD_API_PORT);
        Serial.println("   设备将进入Deep-sleep等待下次唤醒");

        g_shouldEnterDeepSleep = true;
        g_statusChecked = true;
        return;
    }
    
    // 7. 设备已绑定，更新本地状态
    if (!deviceClaimed) {
        deviceClaimed = true;
        saveClaimedStatus(true);
    }
    
    // 8. 检查是否需要更新图片
    Serial.printf("\n📊 图片版本检查: 云端=%d, 本地=%d\n", 
                  status.imageVersion, localImageVersion);
    
    if (status.imageVersion > localImageVersion) {
        if (status.imageUrl.length() == 0) {
            Serial.println("⚠️  云端版本更新但未返回 imageUrl，本次跳过下载，直接Deep-sleep");
            g_shouldEnterDeepSleep = true;
        } else {
            Serial.println("✅ 发现新版本：标记为需要更新（下载/刷新将在 loop 中执行）");
            g_updateNeeded = true;
            g_targetImageVersion = status.imageVersion;
            g_targetImageUrl = status.imageUrl;
        }
    } else {
        Serial.println("✅ 图片已是最新版本，无需更新");
        g_shouldEnterDeepSleep = true;
    }

    // 标记：本次唤醒已完成判定（确保一次性）
    g_statusChecked = true;
}

/* ============================================================================
 *                            初始化和主循环
 * ============================================================================ */

/**
 * HTTP更新模式初始化（在setup中调用）
 */
void HTTP_UPDATE__setup() {
    Serial.println("\n========================================");
    Serial.println("  Deep-sleep + HTTP 更新模式");
    Serial.println("========================================");
    
    // 打印唤醒原因
    printWakeupReason();
    
    // 重置本次唤醒的一次性状态
    g_statusChecked = false;
    g_updateNeeded = false;
    g_updateAttempted = false;
    g_shouldEnterDeepSleep = false;
    g_deepSleepRequested = false;
    g_targetImageVersion = 0;
    g_targetImageUrl = "";

    // 注意：WiFi连接在 wifi_config.h 中完成（.ino 里保证已连上才会进入这里）
    // 本函数只做一次性判定，不做下载/刷新，不在这里立即 deep-sleep
    prepareUpdateDecisionOnce();
}

/**
 * HTTP更新模式主循环（在loop中调用）
 * Deep-sleep架构下loop几乎不会被执行
 */
void HTTP_UPDATE__loop() {
    // 1) 理论上 setup 已经完成一次性判定；如果没有（异常），直接回睡避免耗电
    if (!g_statusChecked) {
        Serial.println("⚠️  未完成更新判定，直接进入Deep-sleep（避免重复/耗电）");
        g_shouldEnterDeepSleep = true;
    }

    // 2) 仅当需要更新时，执行一次下载 + 刷新（只尝试一次，避免 loop 重复下载）
    if (g_updateNeeded && !g_updateAttempted) {
        g_updateAttempted = true;

        Serial.println("\n========================================");
        Serial.println("⬇️  loop: 检测到需要更新，开始下载并刷新...");
        Serial.println("========================================\n");

        if (g_targetImageUrl.length() == 0 || g_targetImageVersion <= 0) {
            Serial.println("⚠️  更新参数不完整，跳过更新");
        } else {
            if (downloadImageToFlash(g_targetImageUrl)) {
                displayDownloadedImage();
                saveImageVersion(g_targetImageVersion);
                localImageVersion = g_targetImageVersion;
                Serial.printf("✅ 已更新到版本: %d\n", localImageVersion);
            } else {
                Serial.println("❌ 下载失败，本次不再重试，直接Deep-sleep");
            }
        }

        // 无论成功与否，本次唤醒都不再重复更新
        g_updateNeeded = false;
        g_shouldEnterDeepSleep = true;
    }

    // 3) 不需要更新：直接回睡（不做重复检查/重复动作）
    if (!g_updateNeeded) {
        g_shouldEnterDeepSleep = true;
    }

    // 4) 进入 deep-sleep（幂等）
    if (g_shouldEnterDeepSleep) {
        enterDeepSleep();
    }

    // 正常不会走到这里
    delay(100);
}

#endif // HTTP_UPDATE_H
