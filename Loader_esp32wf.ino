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

/* ------------------------ 用户自定义：长按进入配网 ------------------------ */
#define WIFI_RECONFIG_HOLD_MS 3000  // 长按GPIO0进入"清除WiFi并AP配网"的阈值（ms）

/**
 * 判断是否为正常唤醒原因（按键/定时器/GPIO等）
 */
static bool isNormalWakeCause(esp_sleep_wakeup_cause_t cause) {
  return (cause == ESP_SLEEP_WAKEUP_TIMER ||
          cause == ESP_SLEEP_WAKEUP_GPIO ||
          cause == ESP_SLEEP_WAKEUP_EXT0 ||
          cause == ESP_SLEEP_WAKEUP_EXT1);
}

/**
 * 检测 GPIO0 是否被持续按住（低电平）达到指定时长
 * 注意：GPIO0 在本项目作为唤醒键（低电平），这里复用它作为"长按配网"入口。
 * 使用 http_update.h 中定义的 WAKEUP_GPIO (GPIO_NUM_0)
 */
static bool isWakeKeyHeldLow(uint32_t holdMs) {
  // WAKEUP_GPIO 在 http_update.h 中已定义为 GPIO_NUM_0
  const gpio_num_t wakeupPin = WAKEUP_GPIO;
  
  pinMode((int)wakeupPin, INPUT_PULLUP);
  gpio_pullup_en(wakeupPin);
  gpio_pulldown_dis(wakeupPin);

  // 必须从一开始就是低电平才算"按住"
  if (gpio_get_level(wakeupPin) != 0) {
    return false;
  }

  uint32_t start = millis();
  while ((millis() - start) < holdMs) {
    if (gpio_get_level(wakeupPin) != 0) {
      return false;  // 中途松开
    }
    delay(10);
  }
  return true;  // 全程按住
}

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

    // 读取唤醒原因
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("⏰ wakeup cause = %d\n", (int)cause);

    // 先读一次"是否已配网"（不改变现有逻辑，仅用于门控）
    bool alreadyConfigured = checkWiFiConfigured();

    // 1) 长按 GPIO0 进入"清除WiFi + AP配网"
    //    - 适用于：从 deep-sleep 按键唤醒后继续按住不放
    //    - 也适用于：上电/复位后按住 GPIO0（若硬件允许）
    if (isWakeKeyHeldLow(WIFI_RECONFIG_HOLD_MS)) {
        Serial.println("🧹 检测到长按GPIO0：清除WiFi配置并进入AP配网模式");
        clearWiFiConfig();       // 清除NVS WiFi信息
        startAPMode();           // 启动AP
        initConfigServer();      // 启动Web配网服务器
        wifiConfigured = false;
        Serial.println("⏳ 等待配网中...（AP模式）");
        return;  // AP模式下不进入Deep-sleep
    }

    // 2) 只保留"GPIO0唤醒/定时唤醒"作为正常工作触发
    //    - 如果是复位/上电等"非正常唤醒"，且已经配过网：直接回睡，不再触发联网更新
    //    - 如果未配网：允许继续走配网流程（否则永远没法配网）
    if (!isNormalWakeCause(cause) && alreadyConfigured) {
        Serial.println("🛑 非按键/非定时唤醒（复位/上电等），且已配网：直接回到Deep-sleep");
        enterDeepSleep();  // 配置唤醒源并入睡（GPIO0+定时器）
        return;
    }
    
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
