/**
  ******************************************************************************
  * @file    epd7in3.h
  * @brief   7.3 inch E6 (7-color) e-Paper driver - 适配层，调用官方Demo驱动
  ******************************************************************************
  */

// 引入官方Demo驱动
#include "EPD_7in3e.h"
#include "DEV_Config.h"  // 用于底层SPI函数
#include <SPIFFS.h>
#include <FS.h>

// 如果FLASH_TEMP_FILE未定义，则定义它（避免包含顺序问题）
#ifndef FLASH_TEMP_FILE
#define FLASH_TEMP_FILE "/temp_image.bin"
#endif

// 这里不直接包含 buff.h，避免在同一个编译单元里重复定义全局变量
// 只做前向声明，真正的定义仍在 buff.h 中，由其它文件（如 mqtt_config.h）包含
extern int  Buff__bufInd;
extern char Buff__bufArr[];
int Buff__getByte(int index);
int Buff__getWord(int index);

// 全局图像缓冲区声明（在mqtt_config.h中定义）
extern UBYTE globalImageBuffer[];
// GLOBAL_IMAGE_BUFFER_SIZE 已在 mqtt_config.h 中定义，这里不再重复定义

// 适配函数：调用官方Demo的初始化
int EPD_7in3E_init() 
{
    Serial.print("\r\nEPD7in3E6 (使用官方Demo驱动)");
    EPD_7IN3E_Init();  // 调用官方Demo的初始化函数
    return 0;
}

// 适配函数：调用官方Demo的显示函数
void EPD_7in3E_Show(void)
{
    EPD_7IN3E_Show();  // 调用官方Demo的显示函数
}

// 适配函数：调用官方Demo的清屏函数
void EPD_7in3E_Clear(byte color)
{
    EPD_7IN3E_Clear((UBYTE)color);  // 调用官方Demo的清屏函数
}

// 适配函数：从Flash加载数据到7.3E6（使用流式处理，避免大内存分配）
// 这个函数会被EPD_dispLoad调用，用于MQTT模式的数据加载
void EPD_load_7in3E_from_buff()
{
    // FLASH_TEMP_FILE已在mqtt_config.h中定义为宏
    
    // 计算需要的缓冲区大小（4bit格式）
    int packedWidth = (EPD_7IN3E_WIDTH + 1) / 2;  // 400字节/行
    int totalBytes = packedWidth * EPD_7IN3E_HEIGHT;
    
    Serial.printf("📥 从Flash读取图像数据: 需要 %d 字节\n", totalBytes);
    Serial.printf("   当前剩余内存: %d 字节\n", ESP.getFreeHeap());
    Serial.printf("   使用流式处理（行缓冲区）\n");
    
    // 打开Flash临时文件
    File file = SPIFFS.open(FLASH_TEMP_FILE, "r");
    if (!file) {
        Serial.println("❌ 无法打开Flash临时文件");
        Serial.println("   可能原因：DOWNLOAD命令未执行或文件未创建");
        return;
    }
    
    int fileSize = file.size();
    Serial.printf("📁 Flash文件大小: %d 字节\n", fileSize);
    
    if (fileSize == 0) {
        Serial.println("❌ Flash文件为空！");
        Serial.println("   可能原因：DOWNLOAD命令未正确执行或数据未写入");
        file.close();
        return;
    }
    
    // 使用行缓冲区（400字节），避免大内存分配
    UBYTE *rowBuffer = (UBYTE *)malloc(packedWidth);
    if (!rowBuffer) {
        Serial.printf("❌ 行缓冲区分配失败！需要 %d 字节，但只有 %d 字节可用\n", 
                      packedWidth, ESP.getFreeHeap());
        file.close();
        return;
    }
    
    Serial.printf("✅ 行缓冲区分配成功: %d 字节\n", packedWidth);
    
    // 在显示前需要初始化EPD（如果还没有初始化）
    Serial.println("   初始化EPD（如果未初始化）...");
    EPD_7IN3E_Init();
    
    // 发送显示命令（0x10）- 开始写入图像数据
    Serial.println("   开始发送图像数据到EPD...");
    DEV_Digital_Write(EPD_DC_PIN, 0);  // 命令模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x10);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 逐行处理：从Flash读取、转换、直接发送到显示驱动
    int charIdx = 0;
    int totalBytesRead = 0;
    
    for (int row = 0; row < EPD_7IN3E_HEIGHT; row++) {
        // 读取一行数据（packedWidth字节，需要2*packedWidth个字符）
        for (int col = 0; col < packedWidth; col++) {
            // 读取两个字符组成一个字节
            if (charIdx >= fileSize || !file.available()) {
                // 数据不足，用白色填充
                rowBuffer[col] = 0x11;  // 两个白色像素
                continue;
            }
            
            char c1 = file.read();
            charIdx++;
            
            if (charIdx >= fileSize || !file.available()) {
                // 只有一个字符，用白色填充
                rowBuffer[col] = 0x11;
                continue;
            }
            
            char c2 = file.read();
            charIdx++;
            
            // 检查是否为有效字符（'a'-'p'）
            if (c1 < 'a' || c1 > 'p' || c2 < 'a' || c2 > 'p') {
                // 无效字符，用白色填充
                rowBuffer[col] = 0x11;
                continue;
            }
            
            // 将两个字符转换为字节
            int low = (c1 - 'a') & 0x0F;
            int high = (c2 - 'a') & 0x0F;
            
            // 打包成字节：高4bit是high，低4bit是low
            rowBuffer[col] = (UBYTE)((high << 4) | low);
            totalBytesRead++;
        }
        
        // 直接发送一行数据到显示驱动（数据模式）
        for (int col = 0; col < packedWidth; col++) {
            DEV_Digital_Write(EPD_DC_PIN, 1);  // 数据模式
            DEV_Digital_Write(EPD_CS_PIN, 0);
            DEV_SPI_WriteByte(rowBuffer[col]);
            DEV_Digital_Write(EPD_CS_PIN, 1);
        }
        
        // 每50行输出一次进度
        if ((row + 1) % 50 == 0) {
            Serial.printf("   进度: %d/%d 行 (%.1f%%)\n", row + 1, EPD_7IN3E_HEIGHT, 
                          (row + 1) * 100.0 / EPD_7IN3E_HEIGHT);
        }
    }
    
    file.close();
    free(rowBuffer);
    
    Serial.printf("✅ 已读取并发送 %d 字节，准备刷新显示\n", totalBytesRead);
    
    // 刷新显示：需要完整的TurnOnDisplay流程
    // 参考EPD_7IN3E_TurnOnDisplay的实现
    Serial.println("   执行完整的显示刷新流程...");
    
    // 1. 发送命令0x04（上电）
    DEV_Digital_Write(EPD_DC_PIN, 0);  // 命令模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x04);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 等待BUSY
    Serial.println("   等待BUSY（上电）...");
    while (!DEV_Digital_Read(EPD_BUSY_PIN)) {
        delay(1);
    }
    
    // 2. 发送命令0x06（设置显示模式）并发送数据
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x06);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 发送数据：0x6F, 0x1F, 0x17, 0x49
    DEV_Digital_Write(EPD_DC_PIN, 1);  // 数据模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x6F);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x1F);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x17);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x49);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 3. 发送命令0x12（显示刷新）并发送数据0x00
    DEV_Digital_Write(EPD_DC_PIN, 0);  // 命令模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x12);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    DEV_Digital_Write(EPD_DC_PIN, 1);  // 数据模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x00);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 等待BUSY（显示刷新）
    Serial.println("   等待BUSY（显示刷新）...");
    while (!DEV_Digital_Read(EPD_BUSY_PIN)) {
        delay(1);
    }
    
    // 4. 发送命令0x02（断电）
    DEV_Digital_Write(EPD_DC_PIN, 0);  // 命令模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x02);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    DEV_Digital_Write(EPD_DC_PIN, 1);  // 数据模式
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(0x00);
    DEV_Digital_Write(EPD_CS_PIN, 1);
    
    // 等待BUSY（断电）
    Serial.println("   等待BUSY（断电）...");
    while (!DEV_Digital_Read(EPD_BUSY_PIN)) {
        delay(1);
    }
    
    Serial.println("✅ 显示完成");
}

