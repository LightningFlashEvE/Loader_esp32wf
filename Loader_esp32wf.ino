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

/* WiFi配置 ------------------------------------------------------------------*/
const char *ssid = "XXGF";          // 改成你的WiFi名称
const char *password = "XXGFNXXGM";  // 改成你的WiFi密码

/* 静态IP配置（可选，默认使用DHCP） ----------------------------------------*/
IPAddress staticIP(192, 168, 10, 166);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(223, 5, 5, 5);

/* MQTT功能 ------------------------------------------------------------------*/
#include "mqtt_config.h"

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
    
    // WiFi连接 - 使用DHCP自动获取IP
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
    
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // MQTT模式初始化（会自动显示设备码）
    MQTT__setup();
    
    Serial.println("✅ 系统就绪，等待云端命令...\n");
}

/* The main loop -------------------------------------------------------------*/
void loop() 
{
    // MQTT模式主循环
    MQTT__loop();
}
