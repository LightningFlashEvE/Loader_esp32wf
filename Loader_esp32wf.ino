/**
 ******************************************************************************
 * @file    Loader_esp32wf.ino
 * @author  Waveshare Team / Modified for MQTT Cloud Control
 * @version V2.0.0
 * @date    23-January-2018 / Modified 2026-01-13
 * @brief   ESP32 E-Paper MQTT Cloud Control
 *          通过云端MQTT服务器远程控制墨水屏显示
 *
 ******************************************************************************
*/ 

/* Includes ------------------------------------------------------------------*/
#include <WiFi.h>

/* WiFi配网功能 ------------------------------------------------------------------*/
#include "wifi_config.h"

/* MQTT功能 ------------------------------------------------------------------*/
#include "mqtt_config.h"

/* 全局变量定义（在头文件中声明为extern）----------------------------------------*/
Preferences preferences;  // NVS持久化存储（供wifi_config和mqtt_config共享）
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
    
    // SPI initialization（保留原有初始化，确保兼容）
    EPD_initSPI();
    
    // WiFi配网初始化
    Serial.println();
    Serial.println("========================================");
    Serial.println("  WiFi配网初始化");
    Serial.println("========================================");
    
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
        return;  // 在AP模式下，不初始化MQTT
    }
    
    // WiFi已连接，继续初始化MQTT
    Serial.println();
    Serial.println("========================================");
    Serial.println("  MQTT云端控制模式");
    Serial.println("========================================");
    
    // MQTT模式初始化（会自动显示设备码）
    MQTT__setup();
    
    Serial.println("✅ 系统就绪，等待云端命令...\n");
}

/* The main loop -------------------------------------------------------------*/
void loop() 
{
    if (wifiConfigured) {
        // WiFi已配置，运行MQTT模式
        MQTT__loop();
    } else {
        // AP配网模式，处理Web服务器请求
        handleAPMode();
    }
}
