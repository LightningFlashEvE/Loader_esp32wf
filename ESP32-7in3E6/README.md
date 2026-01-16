# ESP32-7in3E6 驱动说明

这是7.3寸彩色电子纸屏幕（7.3inch e-Paper E6）的ESP32驱动代码。

## 引脚定义

根据 `DEV_Config.h`，ESP32的接线如下：

| 功能 | ESP32引脚 | 说明 |
|------|-----------|------|
| SCK  | GPIO 13   | SPI时钟 |
| MOSI | GPIO 14   | SPI数据输出 |
| CS   | GPIO 15   | 片选信号 |
| RST  | GPIO 26   | 复位信号 |
| DC   | GPIO 27   | 数据/命令选择 |
| BUSY | GPIO 25   | 忙信号（输入） |
| PWR  | GPIO 33   | 电源控制（可选，需要时启用D_9PIN） |

## 屏幕参数

- 分辨率：800 x 480
- 颜色：7色（黑、白、黄、红、蓝、绿）
- 驱动芯片：支持E6系列

## 驱动文件

- `EPD_7in3e.h` - 驱动头文件
- `EPD_7in3e.cpp` - 驱动实现
- `DEV_Config.h` - 硬件配置（引脚定义）
- `DEV_Config.cpp` - 硬件配置实现

## 集成说明

该驱动已集成到主项目的 `epd.h` 中，索引为 49，使用 `EPD_loadG` 加载函数。

在主项目中，可以通过设置 `EPD_dispIndex = 49` 来使用此屏幕。
