#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
三色墨水屏（黑/白/红）处理算法模块

主要功能：
- 从输入的 PIL.Image（RGB）生成：
  * black_plane: 黑层 bool 数组 (True=黑, False=白)
  * red_plane:   红层 bool 数组 (True=红, False=非红)
  * 可选：预览 RGB 图像
- 可将 bool 位图打包为 1bpp 字节流（按行、MSB->LSB），方便直接下发到 EPD。

依赖：Pillow, numpy
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Tuple, Optional

import numpy as np
from PIL import Image


# ==================== Bayer 有序抖动矩阵 ====================

_BAYER_4 = (1 / 16.0) * np.array(
    [
        [0, 8, 2, 10],
        [12, 4, 14, 6],
        [3, 11, 1, 9],
        [15, 7, 13, 5],
    ],
    dtype=np.float32,
)

_BAYER_8 = (1 / 64.0) * np.array(
    [
        [0, 48, 12, 60, 3, 51, 15, 63],
        [32, 16, 44, 28, 35, 19, 47, 31],
        [8, 56, 4, 52, 11, 59, 7, 55],
        [40, 24, 36, 20, 43, 27, 39, 23],
        [2, 50, 14, 62, 1, 49, 13, 61],
        [34, 18, 46, 30, 33, 17, 45, 29],
        [10, 58, 6, 54, 9, 57, 5, 53],
        [42, 26, 38, 22, 41, 25, 37, 21],
    ],
    dtype=np.float32,
)


def _tile_threshold_matrix(h: int, w: int, base: np.ndarray) -> np.ndarray:
    """把 Bayer 阈值矩阵铺满到指定大小。"""
    bh, bw = base.shape
    rep_y = (h + bh - 1) // bh
    rep_x = (w + bw - 1) // bw
    tiled = np.tile(base, (rep_y, rep_x))
    return tiled[:h, :w]


# ==================== RGB -> HSV（numpy版，H:0~360, S/V:0~1） ====================

def _rgb_to_hsv(rgb: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """rgb: uint8 [H,W,3] -> (h,s,v) float32"""
    r = rgb[..., 0].astype(np.float32) / 255.0
    g = rgb[..., 1].astype(np.float32) / 255.0
    b = rgb[..., 2].astype(np.float32) / 255.0

    cmax = np.maximum(np.maximum(r, g), b)
    cmin = np.minimum(np.minimum(r, g), b)
    delta = cmax - cmin

    # Hue
    h = np.zeros_like(cmax, dtype=np.float32)
    mask = delta > 1e-6

    rm = mask & (cmax == r)
    h[rm] = (60.0 * ((g[rm] - b[rm]) / delta[rm]) + 360.0) % 360.0
    gm = mask & (cmax == g)
    h[gm] = (60.0 * ((b[gm] - r[gm]) / delta[gm]) + 120.0) % 360.0
    bm = mask & (cmax == b)
    h[bm] = (60.0 * ((r[bm] - g[bm]) / delta[bm]) + 240.0) % 360.0

    # Saturation
    s = np.zeros_like(cmax, dtype=np.float32)
    non_zero = cmax > 1e-6
    s[non_zero] = delta[non_zero] / cmax[non_zero]

    v = cmax
    return h, s, v


# ==================== 形态学（3x3）——膨胀/腐蚀/开/闭 ====================

def _pad_bool(a: np.ndarray, pad: int = 1) -> np.ndarray:
    return np.pad(a, ((pad, pad), (pad, pad)), mode="constant", constant_values=False)


def _dilate3(a: np.ndarray, iters: int = 1) -> np.ndarray:
    out = a.astype(bool)
    for _ in range(iters):
        p = _pad_bool(out, 1)
        out = (
            p[0:-2, 0:-2]
            | p[0:-2, 1:-1]
            | p[0:-2, 2:]
            | p[1:-1, 0:-2]
            | p[1:-1, 1:-1]
            | p[1:-1, 2:]
            | p[2:, 0:-2]
            | p[2:, 1:-1]
            | p[2:, 2:]
        )
    return out


def _erode3(a: np.ndarray, iters: int = 1) -> np.ndarray:
    out = a.astype(bool)
    for _ in range(iters):
        p = _pad_bool(out, 1)
        out = (
            p[0:-2, 0:-2]
            & p[0:-2, 1:-1]
            & p[0:-2, 2:]
            & p[1:-1, 0:-2]
            & p[1:-1, 1:-1]
            & p[1:-1, 2:]
            & p[2:, 0:-2]
            & p[2:, 1:-1]
            & p[2:, 2:]
        )
    return out


def _morph_open_close(mask: np.ndarray, open_iters: int = 1, close_iters: int = 1) -> np.ndarray:
    """开(腐蚀->膨胀)去散点 + 闭(膨胀->腐蚀)补小洞"""
    m = _erode3(mask, open_iters)
    m = _dilate3(m, open_iters)
    m = _dilate3(m, close_iters)
    m = _erode3(m, close_iters)
    return m


# ==================== 红掩膜提取 ====================

@dataclass
class RedMaskParams:
    """红色判定与形态学参数，可根据屏幕和风格微调。"""

    h_low: float = 20.0
    h_high: float = 340.0
    s_min: float = 0.35
    v_min: float = 0.25
    rg_min: int = 50
    rb_min: int = 50
    open_iters: int = 1
    close_iters: int = 1


def build_red_mask(rgb: np.ndarray, params: Optional[RedMaskParams] = None) -> np.ndarray:
    """
    自动从 RGB 提取“红色区域”掩膜。

    返回值：
        bool 数组 [H, W]，True 表示该像素属于红层。
    """
    if params is None:
        params = RedMaskParams()

    h, s, v = _rgb_to_hsv(rgb)

    r = rgb[..., 0].astype(np.int16)
    g = rgb[..., 1].astype(np.int16)
    b = rgb[..., 2].astype(np.int16)

    # 方法1：HSV 色相判断（红色：H在0-20或340-360度）
    hue_is_red = (h < params.h_low) | (h > params.h_high)
    sat_ok = s > params.s_min
    val_ok = v > params.v_min
    diff_ok = ((r - g) > params.rg_min) & ((r - b) > params.rb_min)
    method1 = hue_is_red & sat_ok & val_ok & diff_ok

    # 方法2：HSV 色相判断（黄色：H在50-70度，三色屏没有黄色，用红色显示）
    # 黄色：R和G都高，B低，且R和G接近
    hue_is_yellow = (h >= 50.0) & (h <= 70.0)
    yellow_ok = (r > 150) & (g > 150) & (b < 100) & (np.abs(r - g) < 50) & (s > 0.3) & (v > 0.3)
    method2_yellow = hue_is_yellow & yellow_ok

    # 方法3：直接RGB判断（红色：R明显大于G和B，且R足够大）
    method3_red = (r > 100) & (r > g + params.rg_min) & (r > b + params.rb_min)
    
    # 合并所有方法：红色或黄色都识别为红色（因为三色屏没有黄色）
    raw = method1 | method2_yellow | method3_red

    # 形态学处理（如果启用）
    if params.open_iters > 0 or params.close_iters > 0:
        clean = _morph_open_close(raw, open_iters=params.open_iters, close_iters=params.close_iters)
    else:
        clean = raw
    
    return clean


# ==================== 黑白层生成 ====================

def _rgb_to_luma(rgb: np.ndarray) -> np.ndarray:
    """计算亮度 Y，范围 0~1。"""
    r = rgb[..., 0].astype(np.float32) / 255.0
    g = rgb[..., 1].astype(np.float32) / 255.0
    b = rgb[..., 2].astype(np.float32) / 255.0
    return 0.299 * r + 0.587 * g + 0.114 * b


@dataclass
class BlackPlaneParams:
    """黑层生成参数。"""

    gamma: float = 1.0
    threshold: float = 0.5  # 阈值越大，越容易判为黑
    dither: str = "none"  # "none" / "bayer4" / "bayer8"


def build_black_plane(
    rgb: np.ndarray,
    red_mask: np.ndarray,
    params: Optional[BlackPlaneParams] = None,
) -> np.ndarray:
    """
    生成黑层 bool 位图：True=黑，False=白。
    红区域强制为白（避免红里夹黑点）。
    """
    if params is None:
        params = BlackPlaneParams()

    luma = _rgb_to_luma(rgb)  # 0~1，值越大越亮
    if abs(params.gamma - 1.0) > 1e-6:
        luma = np.clip(luma, 0.0, 1.0) ** params.gamma

    h, w = luma.shape

    if params.dither == "none":
        bw_black = luma < params.threshold
    elif params.dither == "bayer4":
        t = _tile_threshold_matrix(h, w, _BAYER_4)
        bw_black = luma < np.clip(params.threshold + (t - 0.5) * 0.25, 0.0, 1.0)
    elif params.dither == "bayer8":
        t = _tile_threshold_matrix(h, w, _BAYER_8)
        bw_black = luma < np.clip(params.threshold + (t - 0.5) * 0.25, 0.0, 1.0)
    else:
        raise ValueError("dither must be one of: 'none', 'bayer4', 'bayer8'")

    # 红优先：红区域不允许黑点
    bw_black = bw_black & (~red_mask)
    return bw_black.astype(bool)


# ==================== 1bpp 打包：按行，MSB->LSB ====================

def pack_1bpp_msb_first(bits_mask: np.ndarray) -> bytes:
    """
    将 bool 位图打包为 1bpp 字节流。

    参数：
        bits_mask: bool [H, W]，True 表示该像素为 1（黑/红）
    返回：
        bytes，按行打包，每行高位在字节 MSB(bit7)。
    """
    h, w = bits_mask.shape
    row_bytes = (w + 7) // 8
    out = bytearray(h * row_bytes)

    for y in range(h):
        base = y * row_bytes
        for x in range(w):
            if bits_mask[y, x]:
                byte_i = base + (x // 8)
                bit = 7 - (x % 8)
                out[byte_i] |= (1 << bit)
    return bytes(out)


# ==================== 封装：从 PIL.Image 得到三色平面 ====================

@dataclass
class TriColorResult:
    """三色处理完整结果。"""

    width: int
    height: int
    black_plane: np.ndarray  # bool [H,W]
    red_plane: np.ndarray  # bool [H,W]
    black_bin: bytes  # 1bpp
    red_bin: bytes  # 1bpp


def process_tricolor_image(
    img: Image.Image,
    target_size: Optional[Tuple[int, int]] = None,
    red_params: Optional[RedMaskParams] = None,
    black_params: Optional[BlackPlaneParams] = None,
) -> TriColorResult:
    """
    对输入 RGB 图像执行三色处理，返回黑/红层位图和打包后的二进制。

    参数：
        img         : PIL.Image，任意模式，内部会转为 RGB。
        target_size : (width, height)，如果提供则先缩放到指定分辨率。
        red_params  : 红掩膜参数，不传则使用默认值。
        black_params: 黑层生成参数，不传则使用默认值。
    """
    rgb_img = img.convert("RGB")
    if target_size is not None:
        rgb_img = rgb_img.resize(target_size, Image.LANCZOS)

    rgb = np.array(rgb_img, dtype=np.uint8)

    red_mask = build_red_mask(rgb, params=red_params)
    black_plane = build_black_plane(rgb, red_mask=red_mask, params=black_params)

    black_bin = pack_1bpp_msb_first(black_plane)
    red_bin = pack_1bpp_msb_first(red_mask.astype(bool))

    h, w, _ = rgb.shape
    return TriColorResult(
        width=w,
        height=h,
        black_plane=black_plane,
        red_plane=red_mask.astype(bool),
        black_bin=black_bin,
        red_bin=red_bin,
    )


def build_preview_image(black_plane: np.ndarray, red_plane: np.ndarray) -> Image.Image:
    """
    根据黑层/红层生成预览 RGB 图像（白底，黑/红覆盖）。
    方便在服务端调试或提供给前端查看。
    注意：红色优先，红色区域不会被黑色覆盖。
    """
    if black_plane.shape != red_plane.shape:
        raise ValueError("black_plane and red_plane must have same shape")

    h, w = black_plane.shape
    out = np.full((h, w, 3), 255, dtype=np.uint8)
    # 先画红色（优先），再画黑色（红色区域不会被覆盖，因为 black_plane 中红色区域应该是 False）
    out[red_plane] = np.array([255, 0, 0], dtype=np.uint8)  # 使用更鲜艳的红色便于预览
    # 只在非红色区域画黑色
    out[black_plane & (~red_plane)] = np.array([0, 0, 0], dtype=np.uint8)
    return Image.fromarray(out, mode="RGB")

