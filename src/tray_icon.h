#pragma once

#include <windows.h>
#include <shellapi.h>

// ─── 托盘菜单命令 ID ──────────────────────────────────────────
#define ID_TRAY_TOGGLE  1001   // 暂停 / 恢复切换
#define ID_TRAY_EXIT    1002   // 退出程序

// ─── 托盘回调消息 ─────────────────────────────────────────────
#define WM_TRAYICON     (WM_USER + 1)

// ─── TrayIcon — 系统托盘模块 ──────────────────────────────────
//
// 职责：
//   - 创建/销毁托盘图标，注册 WM_TRAYICON 回调消息
//   - 创建右键菜单（暂停/恢复、退出）
//   - 动态更新托盘提示文字（已启用 / 已暂停）
//
// 注意：WM_TRAYICON 消息的处理（弹出菜单等）由 AppController 的
//       WndProc 负责，TrayIcon 本身不处理任何消息。
//
class TrayIcon {
public:
    // 创建托盘图标及右键菜单。
    // hostWnd  ：接收 WM_TRAYICON 和 WM_COMMAND 消息的宿主窗口。
    // hInst    ：用于加载图标资源的模块句柄。
    // 返回 true 表示成功，false 表示失败（Shell_NotifyIcon 错误）。
    bool Create(HWND hostWnd, HINSTANCE hInst);

    // 销毁托盘图标、菜单及图标资源。
    void Destroy();

    // 根据启用状态更新托盘提示文字与菜单第一项文字。
    // enabled == true  → 提示"Mouse Finder - 已启用"，菜单项"暂停"
    // enabled == false → 提示"Mouse Finder - 已暂停"，菜单项"恢复"
    void UpdateState(bool enabled);

    // 获取右键菜单句柄（供 AppController 调用 TrackPopupMenu）。
    HMENU GetMenu() const { return m_menu; }

private:
    NOTIFYICONDATA m_nid     = {};
    HWND           m_hwnd    = nullptr;
    HMENU          m_menu    = nullptr;
    bool           m_enabled = true;
};
