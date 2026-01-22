/**
 ******************************************************************************
 * @file    wifi_config.h
 * @brief   WiFi配网功能：AP热点模式 + Web配网页面
 *          支持通过Web页面配置WiFi，完成后自动连接
 ******************************************************************************
*/

#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_mac.h"

// 配网相关配置
// 注意：DEVICE_ID_MODE 应该在 mqtt_config.h 中定义，这里使用默认值 2（后6位）
#ifndef DEVICE_ID_MODE
#define DEVICE_ID_MODE 2  // 设备码模式：1=前6位，2=后6位，其他=完整12位
#endif
#define CONFIG_NAMESPACE "wifi_cfg"   // Preferences命名空间
#define CONFIG_SSID_KEY "ssid"        // WiFi SSID存储键
#define CONFIG_PASSWORD_KEY "pwd"     // WiFi密码存储键
#define CONFIG_CONFIGURED_KEY "cfg"   // 配网标志位存储键

// 全局变量
WebServer server(80);
extern Preferences preferences;  // 在Loader_esp32wf.ino中定义
extern bool wifiConfigured;
String savedSSID = "";
String savedPassword = "";

/**
 * 检查配网状态
 */
bool checkWiFiConfigured() {
    if (!preferences.begin(CONFIG_NAMESPACE, true)) {  // 只读模式
        // NVS命名空间不存在（第一次使用），这是正常的
        preferences.end();
        return false;
    }
    bool configured = preferences.getBool(CONFIG_CONFIGURED_KEY, false);
    if (configured) {
        savedSSID = preferences.getString(CONFIG_SSID_KEY, "");
        savedPassword = preferences.getString(CONFIG_PASSWORD_KEY, "");
    }
    preferences.end();
    return configured && savedSSID.length() > 0;
}

/**
 * 保存WiFi配置
 */
void saveWiFiConfig(String ssid, String password) {
    if (!preferences.begin(CONFIG_NAMESPACE, false)) {  // 读写模式
        Serial.println("⚠️  NVS命名空间打开失败，无法保存WiFi配置");
        return;
    }
    preferences.putString(CONFIG_SSID_KEY, ssid);
    preferences.putString(CONFIG_PASSWORD_KEY, password);
    preferences.putBool(CONFIG_CONFIGURED_KEY, true);
    preferences.end();
    Serial.println("✅ WiFi配置已保存");
}

/**
 * 清除WiFi配置
 */
void clearWiFiConfig() {
    if (!preferences.begin(CONFIG_NAMESPACE, false)) {
        // NVS命名空间不存在，无需清除
        preferences.end();
        return;
    }
    preferences.remove(CONFIG_SSID_KEY);
    preferences.remove(CONFIG_PASSWORD_KEY);
    preferences.putBool(CONFIG_CONFIGURED_KEY, false);
    preferences.end();
    Serial.println("🗑️  WiFi配置已清除");
}

// 前向声明：getDeviceIdFromMac() 在 mqtt_config.h 中定义
// 注意：由于包含顺序，这里需要自己实现获取设备码的逻辑
String getDeviceIdForAP() {
    uint8_t mac[6] = {0};
    
    // 尝试使用esp_read_mac读取MAC地址（使用ESP_MAC_EFUSE_FACTORY类型）
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY);
    if (ret != ESP_OK) {
        Serial.println("⚠️  esp_read_mac(ESP_MAC_EFUSE_FACTORY) 失败，尝试其他方法");
        // 尝试使用WiFi库的方法
        WiFi.macAddress(mac);
        Serial.println("   使用WiFi.macAddress()读取MAC");
    } else {
        Serial.println("✅ 使用esp_read_mac(ESP_MAC_EFUSE_FACTORY)读取MAC");
    }
    
    // 如果MAC地址后三个字节仍为0，尝试使用esp_wifi_get_mac（使用STA接口）
    if (mac[3] == 0 && mac[4] == 0 && mac[5] == 0) {
        Serial.println("⚠️  MAC地址后三个字节为0，尝试esp_wifi_get_mac(WIFI_IF_STA)");
        ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
        if (ret == ESP_OK) {
            Serial.println("✅ 使用esp_wifi_get_mac(WIFI_IF_STA)读取MAC");
        } else {
            // 如果STA接口失败，尝试AP接口
            Serial.println("   尝试esp_wifi_get_mac(WIFI_IF_AP)");
            ret = esp_wifi_get_mac(WIFI_IF_AP, mac);
            if (ret == ESP_OK) {
                Serial.println("✅ 使用esp_wifi_get_mac(WIFI_IF_AP)读取MAC");
            }
        }
    }
    
    // 调试输出：打印完整MAC地址
    Serial.printf("🔍 读取MAC地址: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.printf("   DEVICE_ID_MODE = %d\n", DEVICE_ID_MODE);
    
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
    
    Serial.printf("   提取的设备码: %s\n", buf);
    return String(buf);
}

/**
 * 启动AP热点模式
 */
void startAPMode() {
    Serial.println("📡 启动AP热点模式...");
    
    // 先初始化WiFi（如果还没初始化）
    WiFi.mode(WIFI_AP_STA);  // 先设置为AP+STA模式，确保WiFi已初始化
    delay(100);  // 等待 WiFi 初始化完成
    
    // 获取设备码（在 WiFi 初始化后读取）
    String deviceCode = getDeviceIdForAP();
    
    // 然后切换到纯AP模式
    WiFi.mode(WIFI_AP);
    String apSSID = "EPD-" + deviceCode;
    
    Serial.printf("   AP名称: %s\n", apSSID.c_str());
    Serial.println("   AP密码: 无密码");
    
    // 不设置密码（传入 NULL 或空字符串）
    WiFi.softAP(apSSID.c_str(), NULL);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("   AP IP地址: ");
    Serial.println(IP);
    Serial.println("   请连接到此热点，然后访问: http://192.168.4.1");
}

/**
 * 配网页面HTML
 */
String getConfigPageHTML() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>ESP32 WiFi配网</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; max-width: 400px; margin: 50px auto; padding: 20px; background: #f5f5f5; }";
    html += "h1 { color: #333; text-align: center; }";
    html += ".form-group { margin-bottom: 15px; }";
    html += "label { display: block; margin-bottom: 5px; color: #555; font-weight: bold; }";
    html += "input { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; box-sizing: border-box; }";
    html += "button { width: 100%; padding: 12px; background: #4CAF50; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; }";
    html += "button:hover { background: #45a049; }";
    html += ".status { margin-top: 20px; padding: 10px; border-radius: 5px; text-align: center; }";
    html += ".success { background: #d4edda; color: #155724; }";
    html += ".error { background: #f8d7da; color: #721c24; }";
    html += "</style></head><body>";
    html += "<h1>📶 ESP32 WiFi配网</h1>";
    html += "<form id='wifiForm' onsubmit='return submitConfig(event)'>";
    html += "<div class='form-group'>";
    html += "<label for='ssid'>WiFi名称 (SSID):</label>";
    html += "<input type='text' id='ssid' name='ssid' required placeholder='请输入WiFi名称'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='password'>WiFi密码:</label>";
    html += "<input type='password' id='password' name='password' placeholder='请输入WiFi密码（可选）'>";
    html += "</div>";
    html += "<button type='submit'>连接WiFi</button>";
    html += "</form>";
    html += "<div id='status'></div>";
    html += "<script>";
    html += "function submitConfig(e) {";
    html += "  e.preventDefault();";
    html += "  var ssid = document.getElementById('ssid').value;";
    html += "  var password = document.getElementById('password').value;";
    html += "  var statusDiv = document.getElementById('status');";
    html += "  statusDiv.innerHTML = '<div class=\"status\">正在连接，请稍候...</div>';";
    html += "  fetch('/config', {";
    html += "    method: 'POST',";
    html += "    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },";
    html += "    body: 'ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password)";
    html += "  }).then(response => response.text())";
    html += "    .then(data => {";
    html += "      if (data.includes('success')) {";
    html += "        statusDiv.innerHTML = '<div class=\"status success\">✅ 配置成功！设备正在重启并连接WiFi...</div>';";
    html += "        setTimeout(() => { statusDiv.innerHTML += '<p>如果连接失败，请重新连接AP热点</p>'; }, 2000);";
    html += "      } else {";
    html += "        statusDiv.innerHTML = '<div class=\"status error\">❌ 配置失败: ' + data + '</div>';";
    html += "      }";
    html += "    }).catch(err => {";
    html += "      statusDiv.innerHTML = '<div class=\"status error\">❌ 请求失败: ' + err + '</div>';";
    html += "    });";
    html += "  return false;";
    html += "}";
    html += "</script></body></html>";
    return html;
}

/**
 * 处理根路径请求（配网页面）
 */
void handleRoot() {
    server.send(200, "text/html", getConfigPageHTML());
}

/**
 * 处理WiFi配置提交
 */
void handleConfig() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    
    if (ssid.length() == 0) {
        server.send(400, "text/plain", "SSID不能为空");
        return;
    }
    
    Serial.println("📝 收到WiFi配置:");
    Serial.printf("   SSID: %s\n", ssid.c_str());
    Serial.printf("   密码: %s\n", password.length() > 0 ? "***" : "(无密码)");
    
    // 保存配置
    saveWiFiConfig(ssid, password);
    
    server.send(200, "text/plain", "success");
    
    // 延迟后重启
    Serial.println("⏳ 3秒后重启并连接WiFi...");
    delay(3000);
    ESP.restart();
}

/**
 * 处理扫描WiFi请求
 */
void handleScan() {
    Serial.println("📡 扫描WiFi网络...");
    int n = WiFi.scanNetworks();
    
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
        json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
        json += "\"encryption\":" + String(WiFi.encryptionType(i));
        json += "}";
    }
    json += "]";
    
    server.send(200, "application/json", json);
}

/**
 * 初始化Web服务器（AP模式）
 */
void initConfigServer() {
    server.on("/", handleRoot);
    server.on("/config", handleConfig);
    server.on("/scan", handleScan);
    server.begin();
    Serial.println("✅ Web配网服务器已启动");
}

/**
 * 连接WiFi（使用保存的配置）
 */
bool connectWiFi() {
    if (!checkWiFiConfigured()) {
        Serial.println("⚠️  未检测到WiFi配置，将进入AP配网模式");
        return false;
    }
    
    Serial.println("📶 使用保存的WiFi配置连接...");
    Serial.printf("   SSID: %s\n", savedSSID.c_str());
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("");
        Serial.println("✅ WiFi连接成功");
        Serial.print("   IP地址: ");
        Serial.println(WiFi.localIP());
        return true;
    } else {
        Serial.println("");
        Serial.println("❌ WiFi连接失败");
        return false;
    }
}

/**
 * WiFi配网初始化
 * 返回值：true=已连接WiFi，false=进入AP配网模式
 */
bool initWiFiConfig() {
    // 检查配网状态
    if (checkWiFiConfigured()) {
        // 尝试连接WiFi
        if (connectWiFi()) {
            wifiConfigured = true;
            return true;
        } else {
            // 连接失败，清除配置，进入AP模式
            Serial.println("⚠️  WiFi连接失败，清除配置并进入AP配网模式");
            clearWiFiConfig();
        }
    }
    
    // 进入AP配网模式
    startAPMode();
    initConfigServer();
    wifiConfigured = false;
    return false;
}

/**
 * AP模式循环处理（需要在loop中调用）
 */
void handleAPMode() {
    if (!wifiConfigured) {
        server.handleClient();
    }
}

#endif // WIFI_CONFIG_H
