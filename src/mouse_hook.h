#pragma once

#include <windows.h>

// WM_SHAKE_DETECTED 消息：当鼠标抖动检测触发时由 MouseHook 发送给通知窗口
#define WM_SHAKE_DETECTED (WM_USER + 100)

// MouseHook —— 鼠标钩子模块
// 职责：安装/卸载 WH_MOUSE_LL 全局钩子，检测鼠标水平方向反转次数（抖动检测），
//       在时间窗口内累计反转次数达到 SHAKE_COUNT 时向主窗口 PostMessage(WM_SHAKE_DETECTED)
class MouseHook {
public:
    // 安装全局低级鼠标钩子。notifyWnd 是接收 WM_SHAKE_DETECTED 的目标窗口。
    // 成功返回 true，失败返回 false（SetWindowsHookEx 失败）。
    bool Install(HWND notifyWnd);

    // 卸载钩子。若未安装则为空操作。
    void Uninstall();

    // 启用或禁用抖动检测。禁用时 CheckShake 不处理任何移动事件。
    void SetEnabled(bool enabled);

private:
    // 低级鼠标钩子回调（系统注入，在安装钩子的线程消息循环中调用）
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    // 方向反转计数抖动检测算法
    // pos：当前鼠标屏幕坐标；now：当前时间戳（GetTickCount，ms）
    // 返回 true 表示检测到抖动（应触发放大）
    bool CheckShake(POINT pos, DWORD now);

    // 静态实例指针：供静态回调 LowLevelMouseProc 访问 this
    static MouseHook* s_instance;

    HHOOK  m_hook    = nullptr; // 钩子句柄
    HWND   m_notify  = nullptr; // 通知窗口句柄
    bool   m_enabled = true;    // 是否启用抖动检测

    // 抖动检测状态
    int    m_lastX         = 0; // 上一次有效鼠标 X 坐标
    int    m_lastDir       = 0; // 上一次水平方向（+1 向右，-1 向左，0 未初始化）
    int    m_reversalCount = 0; // 当前时间窗口内的方向反转次数
    DWORD  m_windowStart   = 0; // 时间窗口起始时间戳（ms）
};
