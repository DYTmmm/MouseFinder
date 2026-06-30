#include "tray_icon.h"
#include "resource.h"
#include <strsafe.h>

// ─── TrayIcon::Create ─────────────────────────────────────────
bool TrayIcon::Create(HWND hostWnd, HINSTANCE hInst)
{
    m_hwnd = hostWnd;

    // ── 加载托盘图标 ──────────────────────────────────────────
    // 优先使用 LoadImage 以支持非标准尺寸；若失败则回退到 LoadIcon。
    HICON hIcon = static_cast<HICON>(
        LoadImageW(hInst,
                   MAKEINTRESOURCEW(IDI_TRAY),
                   IMAGE_ICON,
                   GetSystemMetrics(SM_CXSMICON),
                   GetSystemMetrics(SM_CYSMICON),
                   LR_DEFAULTCOLOR));
    if (!hIcon)
    {
        hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_TRAY));
    }
    if (!hIcon)
    {
        // 最后回退：使用系统默认应用程序图标
        hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    // ── 填充 NOTIFYICONDATA ───────────────────────────────────
    m_nid.cbSize           = sizeof(NOTIFYICONDATA);
    m_nid.hWnd             = hostWnd;
    m_nid.uID              = 1;                    // 托盘图标 ID（固定为 1）
    m_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon            = hIcon;

    // 初始提示文字：已启用
    StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip),
                   L"Mouse Finder - 已启用");

    if (!Shell_NotifyIconW(NIM_ADD, &m_nid))
    {
        // Shell_NotifyIcon 失败时释放图标，避免泄漏
        if (hIcon)
            DestroyIcon(hIcon);
        m_nid.hIcon = nullptr;
        return false;
    }

    // ── 创建右键菜单 ──────────────────────────────────────────
    m_menu = CreatePopupMenu();
    if (m_menu)
    {
        AppendMenuW(m_menu, MF_STRING, ID_TRAY_TOGGLE, L"暂停");
        AppendMenuW(m_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m_menu, MF_STRING, ID_TRAY_EXIT,   L"退出");
    }

    m_enabled = true;
    return true;
}

// ─── TrayIcon::Destroy ────────────────────────────────────────
void TrayIcon::Destroy()
{
    // 从系统托盘移除图标
    if (m_nid.hWnd)
    {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
    }

    // 销毁右键菜单
    if (m_menu)
    {
        DestroyMenu(m_menu);
        m_menu = nullptr;
    }

    // 销毁图标句柄
    if (m_nid.hIcon)
    {
        DestroyIcon(m_nid.hIcon);
        m_nid.hIcon = nullptr;
    }

    m_nid.hWnd = nullptr;
    m_hwnd     = nullptr;
}

// ─── TrayIcon::UpdateState ────────────────────────────────────
void TrayIcon::UpdateState(bool enabled)
{
    m_enabled = enabled;

    // 更新提示文字
    StringCchCopyW(m_nid.szTip, ARRAYSIZE(m_nid.szTip),
                   enabled ? L"Mouse Finder - 已启用"
                           : L"Mouse Finder - 已暂停");

    // 刷新托盘提示
    m_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;   // 恢复完整标志

    // 更新菜单第一项（暂停 ↔ 恢复）
    if (m_menu)
    {
        MENUITEMINFOW mii  = {};
        mii.cbSize         = sizeof(MENUITEMINFOW);
        mii.fMask          = MIIM_STRING;
        mii.dwTypeData     = enabled ? const_cast<LPWSTR>(L"暂停")
                                     : const_cast<LPWSTR>(L"恢复");
        SetMenuItemInfoW(m_menu, ID_TRAY_TOGGLE, FALSE, &mii);
    }
}
