#pragma once

#include <windows.h>

namespace Config {

    // ─── 抖动检测参数 ──────────────────────────────────────────────
    // 触发所需的水平方向反转次数
    constexpr int   SHAKE_COUNT      = 4;

    // 检测时间窗口（ms）：窗口内完成 SHAKE_COUNT 次反转即触发
    constexpr DWORD SHAKE_WINDOW_MS  = 1000;

    // 单次移动最小像素数，低于此值的移动事件被视为噪声忽略
    constexpr int   MIN_MOVE_PIXELS  = 10;

    // ─── 动画参数 ──────────────────────────────────────────────────
    // 峰值放大倍数
    constexpr float MAX_SCALE_FACTOR = 5.0f;

    // 放大后保持峰值的持续时长（ms）
    constexpr DWORD HOLD_DURATION    = 500;

    // 缩小动画时长（ms）
    constexpr DWORD SHRINK_DURATION  = 1500;

    // 定时器间隔（ms），约 120fps，使缩小动画更丝滑
    constexpr DWORD TIMER_INTERVAL   = 8;

} // namespace Config
