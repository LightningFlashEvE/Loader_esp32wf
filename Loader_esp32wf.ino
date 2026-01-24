/**
 ******************************************************************************
 * @file    Loader_esp32wf.ino
 * @author  Waveshare Team / Modified for Deep-sleep + HTTP Pull
 * @version V3.0.0
 * @date    23-January-2018 / Modified 2026-01-24
 * @brief   ESP32 E-Paper Deep-sleep + HTTP Pull Update
 *          设备绝大多数时间处于Deep-sleep，只有按键或定时唤醒后
 *          才联网HTTP拉取更新图片，刷新墨水屏后立即回到Deep-sleep
 *
 ******************************************************************************
*/ 

/* Includes ------------------------------------------------------------------*/
#include <WiFi.h>

/* WiFi配网功能 ------------------------------------------------------------------*/
#include "wifi_config.h"

/* HTTP更新功能（替代原MQTT） --------------------------------------------------*/
#include "http_update.h"

/* 全局变量定义（在头文件中声明为extern）----------------------------------------*/
Preferences preferences;  // NVS持久化存储（供wifi_config和http_update共享）
bool wifiConfigured = false;  // WiFi配网状态标志

/* Entry point ----------------------------------------------------------------*/
void setup() 
{
    // Serial port initialization
    Serial.begin(115200);
    delay(10);
    
    // 初始化官方Demo的硬件接口
    #include "DEV_Config.h"
    DEV_Module_Init();
    
    // SPI initialization
    EPD_initSPI();
    
    // 打印启动信息
    Serial.println();
    Serial.println("========================================");
    Serial.println("  ESP32 E-Paper Deep-sleep 模式");
    Serial.println("  Version 3.0.0");
    Serial.println("========================================");
    Serial.printf("  剩余内存: %d 字节\n", ESP.getFreeHeap());
    Serial.println("========================================\n");
    
    // WiFi配网初始化
    Serial.println("📶 WiFi配网初始化...");
    
    bool wifiConnected = initWiFiConfig();
    
    if (!wifiConnected) {
        // AP配网模式
        Serial.println();
        Serial.println("📱 设备已进入AP配网模式");
        Serial.println("   请按以下步骤操作：");
        Serial.println("   1. 连接WiFi热点（名称见上方）");
        Serial.println("   2. 访问 http://192.168.4.1");
        Serial.println("   3. 输入WiFi名称和密码");
        Serial.println("   4. 点击连接，设备将自动重启");
        Serial.println();
        Serial.println("⏳ 等待配网中...（AP模式）");
        // 注意：AP配网模式下不进入Deep-sleep，保持Web服务器运行
        return;
    }
    
    // WiFi已连接，执行HTTP更新检查
    Serial.println();
    Serial.println("✅ WiFi已连接，开始HTTP更新检查...");
    
    // HTTP更新模式初始化：本次唤醒只做一次“是否需要更新”的判定
    HTTP_UPDATE__setup();
    
    // 为了避免进入 loop 后再做一次兜底，这里直接调用一次 loop 处理：
    // - 需要更新：执行下载+刷新，然后 deep-sleep
    // - 不需要更新：直接 deep-sleep
    HTTP_UPDATE__loop();

    // 正常情况下不会执行到这里（deep-sleep 后不会返回）
    Serial.println("⚠️  仍在运行：未进入Deep-sleep（异常路径）");
}

/* The main loop -------------------------------------------------------------*/
void loop() 
{
    if (wifiConfigured) {
        // WiFi已配置，正常情况下不会执行到这里
        // 因为setup()中的HTTP_UPDATE__setup()会进入Deep-sleep
        // 如果执行到这里，尝试重新进入Deep-sleep
        HTTP_UPDATE__loop();
    } else {
        // AP配网模式，处理Web服务器请求
        handleAPMode();
    }
}
