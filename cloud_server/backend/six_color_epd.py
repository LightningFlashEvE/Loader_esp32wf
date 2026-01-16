#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
7.3寸E6六色墨水屏（黑/白/红/绿/蓝/黄）处理算法模块

主要功能：
- 使用Floyd-Steinberg抖动算法将RGB图像映射到6色调色板
- 转换为4bit格式（每字节两个像素）
- 生成预览图像

依赖：Pillow, numpy
"""

from __future__ import annotations

from typing import Tuple, Optional
import io
import base64

import numpy as np
from PIL import Image


# ==================== E6 六色调色板 ====================
# 颜色索引：0=黑, 1=白, 2=黄, 3=红, 5=蓝, 6=绿
# 注意：索引4未使用（官方定义中橙色被注释掉了）

# RGB到驱动索引的映射（对应EPD_7in3e.h中的定义）
E6_RGB2IDX = {
    (0,   0,   0  ): 0x0,  # 黑色   -> EPD_7IN3E_BLACK
    (255, 255, 255): 0x1,  # 白色   -> EPD_7IN3E_WHITE
    (255, 255, 0  ): 0x2,  # 黄色   -> EPD_7IN3E_YELLOW
    (255, 0,   0  ): 0x3,  # 红色   -> EPD_7IN3E_RED
    (0,   0,   255): 0x5,  # 蓝色   -> EPD_7IN3E_BLUE
    (0,   255, 0  ): 0x6,  # 绿色   -> EPD_7IN3E_GREEN
}

# 驱动索引到RGB的反向映射（用于预览）
E6_IDX2RGB = {
    0x0: (0,   0,   0  ),  # 黑色
    0x1: (255, 255, 255),  # 白色
    0x2: (255, 255, 0  ),  # 黄色
    0x3: (255, 0,   0  ),  # 红色
    0x5: (0,   0,   255),  # 蓝色
    0x6: (0,   255, 0  ),  # 绿色
}

# 构建PIL调色板（需要768个值，RGB各256）
def build_pil_palette() -> list:
    """
    构建PIL调色板格式（768个值）
    只包含我们定义的6种颜色，其余位置用白色填充
    """
    palette = []
    # 添加6种颜色的RGB值
    for rgb in E6_RGB2IDX.keys():
        palette.extend(rgb)
    # 填充到256色（768个值），剩余位置用白色填充
    while len(palette) < 768:
        palette.extend([255, 255, 255])  # 白色
    # 确保正好是768个值
    palette = palette[:768]
    return palette


# ==================== Floyd-Steinberg 抖动处理 ====================

def convert_to_e6_with_dither(img: Image.Image, target_size: Optional[Tuple[int, int]] = None) -> Tuple[Image.Image, np.ndarray]:
    """
    将RGB图像转换为E6六色格式，使用Floyd-Steinberg抖动
    
    参数：
        img: PIL.Image，任意模式，内部会转为RGB
        target_size: (width, height)，如果提供则先缩放到指定分辨率
    
    返回：
        (paletted_image, color_indices)
        - paletted_image: PIL.Image (P模式，调色板索引)
        - color_indices: numpy数组 [H, W]，值为0-6（颜色索引）
    """
    # 转换为RGB并调整尺寸
    rgb_img = img.convert("RGB")
    if target_size is not None:
        rgb_img = rgb_img.resize(target_size, Image.LANCZOS)
    
    width, height = rgb_img.size
    
    # 构建调色板
    palette_data = build_pil_palette()
    
    # 创建调色板图像（P模式，256色）
    # 先创建一个包含所有6种颜色的调色板图像
    palette_img = Image.new("P", (1, 1))
    palette_img.putpalette(palette_data)
    
    # 使用PIL的quantize方法，配合Floyd-Steinberg抖动
    # 这会自动将图像映射到最接近的调色板颜色，并使用误差扩散
    paletted = rgb_img.quantize(
        palette=palette_img,
        dither=Image.FLOYDSTEINBERG
    )
    
    # 获取quantize后的palette索引（0-255）
    palette_indices = np.array(paletted, dtype=np.uint8)
    
    # 获取调色板的RGB值
    pal = paletted.getpalette()  # 返回768个值的列表
    
    # 将palette索引映射到驱动颜色索引（0x0-0x6）
    # quantize返回的是palette中的索引，我们需要：
    # 1. 从palette中获取RGB值
    # 2. 通过RGB值查找对应的驱动索引
    # 使用向量化操作提高效率
    height, width = palette_indices.shape
    mapped_indices = np.zeros_like(palette_indices, dtype=np.uint8)
    
    # 创建palette索引到驱动索引的查找表（0-255 -> 0x0-0x6）
    # 使用numpy数组实现高效的向量化查找
    palette_to_driver = np.zeros(256, dtype=np.uint8)
    
    for pal_idx in range(256):
        r = pal[pal_idx * 3 + 0]
        g = pal[pal_idx * 3 + 1]
        b = pal[pal_idx * 3 + 2]
        rgb = (r, g, b)
        
        if rgb in E6_RGB2IDX:
            palette_to_driver[pal_idx] = E6_RGB2IDX[rgb]
        else:
            # 如果找不到精确匹配，找最接近的颜色
            # 使用欧氏距离找最接近的RGB
            min_dist = float('inf')
            closest_idx = 0x1  # 默认白色
            for target_rgb, target_idx in E6_RGB2IDX.items():
                dist = sum((a - b) ** 2 for a, b in zip(rgb, target_rgb))
                if dist < min_dist:
                    min_dist = dist
                    closest_idx = target_idx
            palette_to_driver[pal_idx] = closest_idx
    
    # 使用向量化查找表映射（高效）
    mapped_indices = palette_to_driver[palette_indices]
    
    return paletted, mapped_indices


def convert_indices_to_4bit(color_indices: np.ndarray) -> bytes:
    """
    将颜色索引数组转换为4bit格式（每字节两个像素）
    
    参数：
        color_indices: numpy数组 [H, W]，值为驱动索引（0x0, 0x1, 0x2, 0x3, 0x5, 0x6）
    
    返回：
        bytes: 4bit打包后的数据
    """
    height, width = color_indices.shape
    packed_width = (width + 1) // 2  # 向上取整
    output = bytearray(height * packed_width)
    
    for y in range(height):
        for x in range(0, width, 2):
            # 获取两个像素的驱动索引
            idx1 = int(color_indices[y, x]) & 0xF  # 确保只取低4bit
            idx2 = (int(color_indices[y, x + 1]) & 0xF) if x + 1 < width else 0x1  # 默认白色
            
            # 打包：高4bit是第一个像素，低4bit是第二个像素
            packed_byte = (idx1 << 4) | idx2
            output[y * packed_width + x // 2] = packed_byte
    
    return bytes(output)


def build_preview_image(color_indices: np.ndarray) -> Image.Image:
    """
    根据颜色索引生成预览RGB图像
    
    参数：
        color_indices: numpy数组 [H, W]，值为驱动索引（0x0, 0x1, 0x2, 0x3, 0x5, 0x6）
    
    返回：
        PIL.Image (RGB模式)
    """
    height, width = color_indices.shape
    rgb_array = np.zeros((height, width, 3), dtype=np.uint8)
    
    # 映射驱动索引到RGB值
    for idx, rgb in E6_IDX2RGB.items():
        mask = (color_indices == idx)
        if np.any(mask):
            rgb_array[mask] = rgb
    
    # 处理无效索引（默认白色）
    valid_indices = set(E6_IDX2RGB.keys())
    invalid_mask = ~np.isin(color_indices, list(valid_indices))
    if np.any(invalid_mask):
        rgb_array[invalid_mask] = E6_IDX2RGB[0x1]  # 白色
    
    return Image.fromarray(rgb_array, mode="RGB")


def process_e6_image(
    img: Image.Image,
    target_size: Optional[Tuple[int, int]] = None
) -> dict:
    """
    完整的E6图像处理流程
    
    参数：
        img: PIL.Image
        target_size: (width, height)，默认(800, 480)
    
    返回：
        {
            'preview_image': PIL.Image (RGB预览),
            'color_indices': np.ndarray (颜色索引),
            'data_4bit': bytes (4bit打包数据),
            'width': int,
            'height': int
        }
    """
    if target_size is None:
        target_size = (800, 480)
    
    # 转换为E6格式
    paletted, color_indices = convert_to_e6_with_dither(img, target_size)
    
    # 生成预览
    preview = build_preview_image(color_indices)
    
    # 转换为4bit格式
    data_4bit = convert_indices_to_4bit(color_indices)
    
    return {
        'preview_image': preview,
        'color_indices': color_indices,  # numpy数组，需要转换为列表以便JSON序列化
        'data_4bit': data_4bit,
        'width': target_size[0],
        'height': target_size[1]
    }


# ==================== Flask API 辅助函数 ====================

def process_e6_image_from_base64(base64_data: str, width: int = 800, height: int = 480) -> dict:
    """
    从base64字符串处理图像
    
    参数：
        base64_data: base64编码的图片数据（不含data:image前缀）
        width: 目标宽度
        height: 目标高度
    
    返回：
        包含预览图base64、4bit数据base64等的字典
    """
    # 解码base64
    img_bytes = base64.b64decode(base64_data)
    img = Image.open(io.BytesIO(img_bytes))
    
    # 处理图像
    result = process_e6_image(img, target_size=(width, height))
    
    # 将预览图转换为base64
    preview_buffer = io.BytesIO()
    result['preview_image'].save(preview_buffer, format='PNG')
    preview_base64 = base64.b64encode(preview_buffer.getvalue()).decode('utf-8')
    
    # 将4bit数据转换为base64
    data_4bit_base64 = base64.b64encode(result['data_4bit']).decode('utf-8')
    
    # 将颜色索引数组展平为一维列表（按行扫描）
    color_indices_flat = result['color_indices'].flatten().tolist()
    
    return {
        'success': True,
        'previewImage': preview_base64,
        'data4bit': data_4bit_base64,
        'width': result['width'],
        'height': result['height'],
        'colorIndices': color_indices_flat  # 一维列表，便于前端使用
    }
