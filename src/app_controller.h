#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "cursor_scaler.h"
#include "cursor_animator.h"
#include "mouse_hook.h"
#include "tray_icon.h"
#include "startup_manager.h"

// AppController —— 应用控制器
// 职责：
//   - 初始化所有子模块（CursorScaler、CursorAnimator、MouseHook、TrayIcon、StartupManager）
//   - 持有全局启用 / 禁用状态
//   - 创建隐藏消息窗口，处理 WM_SHAKE_DETECTED、WM_TRAYICON、WM_COMMAND、WM_DESTROY
//   - 运行主消息循环，直到用户选择"退出"
class AppController {
public:
    // 初始化所有模块并创建隐藏消息窗口。
    // 返回 true 表示初始化成功；false 表示致命错误（窗口注册/创建失败）。
    bool Initialize(HINSTANCE hInstance);

    // 主消息循环，阻塞直到 PostQuitMessage 被调用。
    void Run();

    // 关闭所有模块并销毁窗口。
    void Shutdown();

    // 设置启用 / 禁用状态，同步到 MouseHook 和 TrayIcon。
    void SetEnabled(bool enabled);

    // 返回当前是否处于启用状态。
    bool IsEnabled() const { return m_enabled; }

private:
    // 隐藏消息窗口的窗口过程回调（静态，通过 s_instance 访问成员）。
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // 静态实例指针，供 WndProc 访问非静态成员。
    static AppController* s_instance;

    HWND      m_hwnd    = nullptr;
    HINSTANCE m_hInst   = nullptr;
    bool      m_enabled = true;

    CursorScaler   m_scaler;
    CursorAnimator m_animator;
    MouseHook      m_hook;
    TrayIcon       m_tray;
    StartupManager m_startup;
};
