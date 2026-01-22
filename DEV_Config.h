/*****************************************************************************
* | File      	:   DEV_Config.h
* | Author      :   Waveshare team
* | Function    :   Hardware underlying interface
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2020-02-19
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/
#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_

#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>

// 数据类型
#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

// GPIO配置（与epd.h保持一致）
// 为了兼容合宙 ESP32C3-Core，这里做了简单的芯片区分：
// - 默认分支：老款 ESP32（如 NodeMCU、DevKitC），保持原引脚不变
// - ESP32C3 分支：推荐一套适合 ESP32C3-Core 的连线与引脚（可根据你实际接线修改）
//
// ⚠️ 请务必确认你的实际接线，再对下面 ESP32C3 的引脚号做对应修改。
#if defined(ARDUINO_ESP32C3_DEV) || defined(CONFIG_IDF_TARGET_ESP32C3)
    // ===== 合宙 ESP32C3-Core 推荐连接 =====
    // 建议硬件连线（可按此方式接线，或按自己布线修改宏）：
    //   EPD_SCK  <-> GPIO2
    //   EPD_MOSI <-> GPIO7
    //   EPD_CS   <-> GPIO10
    //   EPD_RST  <-> GPIO4
    //   EPD_DC   <-> GPIO5
    //   EPD_BUSY <-> GPIO6
    //
    // 说明：
    // - 避开 GPIO8 / GPIO9（与下载模式相关）以及 GPIO11（Flash 相关）
    // - 所有这些脚在 C3 上都是普通可用 GPIO
    #define EPD_SCK_PIN  2
    #define EPD_MOSI_PIN 7
    #define EPD_CS_PIN   10
    #define EPD_RST_PIN  4
    #define EPD_DC_PIN   5
    #define EPD_BUSY_PIN 6
#else
    // ===== 传统 ESP32 开发板（原项目） =====
    #define EPD_SCK_PIN  13
    #define EPD_MOSI_PIN 14
    #define EPD_CS_PIN   15
    #define EPD_RST_PIN  26
    #define EPD_DC_PIN   27
    #define EPD_BUSY_PIN 25
#endif

// 根据实际使用的硬件启用或禁用，以及对应的引脚
#define D_9PIN  0
#if D_9PIN
    #define EPD_PWR_PIN 33
#endif

#define GPIO_PIN_SET   1
#define GPIO_PIN_RESET 0

// GPIO读写
#define DEV_Digital_Write(_pin, _value) digitalWrite(_pin, _value == 0? LOW:HIGH)
#define DEV_Digital_Read(_pin) digitalRead(_pin)

// 延迟x毫秒
#define DEV_Delay_ms(__xms) delay(__xms)

// 函数声明
UBYTE DEV_Module_Init(void);
void GPIO_Mode(UWORD GPIO_Pin, UWORD Mode);
void DEV_SPI_WriteByte(UBYTE data);
UBYTE DEV_SPI_ReadByte();
void DEV_SPI_Write_nByte(UBYTE *pData, UDOUBLE len);
void DEV_Module_Exit(void);

#endif
