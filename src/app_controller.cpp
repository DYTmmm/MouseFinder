#include "app_controller.h"
#include "mouse_hook.h"
#include "tray_icon.h"
#include "cursor_animator.h"

// 静态成员定义
AppController* AppController::s_instance = nullptr;

// ---------------------------------------------------------------------------
// Initialize — 初始化所有模块，创建隐藏消息窗口
// ---------------------------------------------------------------------------
bool AppController::Initialize(HINSTANCE hInstance)
{
    m_hInst    = hInstance;
    s_instance = this;

    // ── 注册隐藏消息窗口类 ────────────────────────────────────────────
    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"MouseFinderHost";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));

    if (!RegisterClassExW(&wc))
    {
        // 若类已注册（重复初始化），GetLastError 返回 ERROR_CLASS_ALREADY_EXISTS，可继续
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
    }

    // ── 创建隐藏消息窗口（不可见，仅用于接收消息） ───────────────────
    m_hwnd = CreateWindowExW(
        0,
        L"MouseFinderHost",
        L"Mouse Finder",
        WS_OVERLAPPED,
        0, 0, 0, 0,
        HWND_MESSAGE,   // 使用消息-only 窗口，不出现在任务栏
        nullptr,
        hInstance,
        nullptr);

    if (!m_hwnd)
        return false;

    // ── 捕获系统光标（方案 C：按需预计算，此处无需初始化） ───────────────

    // ── 初始化动画模块 ────────────────────────────────────────────────
    m_animator.Initialize(&m_scaler, m_hwnd);

    // ── 安装鼠标钩子 ──────────────────────────────────────────────────
    if (!m_hook.Install(m_hwnd))
    {
        // 钩子安装失败：提示用户，但程序仍可运行（托盘功能正常，抖动检测不可用）
        MessageBoxW(m_hwnd,
            L"鼠标钩子安装失败，抖动检测功能不可用。\n程序将以禁用状态启动。",
            L"Mouse Finder",
            MB_ICONWARNING | MB_OK);
        m_enabled = false;
    }

    // ── 创建系统托盘图标 ──────────────────────────────────────────────
    m_tray.Create(m_hwnd, hInstance);
    m_tray.UpdateState(m_enabled);

    // ── 开机自启检查（首次运行弹框询问） ─────────────────────────────
    m_startup.CheckAndPrompt(m_hwnd);

    return true;
}

// ---------------------------------------------------------------------------
// Run — 主消息循环，阻塞直到 PostQuitMessage
// ---------------------------------------------------------------------------
void AppController::Run()
{
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ---------------------------------------------------------------------------
// Shutdown — 关闭所有模块，销毁窗口
// ---------------------------------------------------------------------------
void AppController::Shutdown()
{
    m_hook.Uninstall();
    m_animator.Shutdown();
    m_tray.Destroy();

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ---------------------------------------------------------------------------
// SetEnabled — 同步启用/禁用状态到 MouseHook 和 TrayIcon
// ---------------------------------------------------------------------------
void AppController::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    m_hook.SetEnabled(enabled);
    m_tray.UpdateState(enabled);
}

// ---------------------------------------------------------------------------
// WndProc — 隐藏消息窗口的消息处理回调
// ---------------------------------------------------------------------------
LRESULT CALLBACK AppController::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AppController* self = s_instance;

    switch (msg)
    {
    // ── 动画定时器 tick（由 CursorAnimator 的定时器线程 PostMessage 过来）─
    case WM_ANIM_TICK:
        if (self) self->m_animator.Tick();
        return 0;

    // ── 鼠标抖动检测触发 ────────────────────────────────────────────
    case WM_SHAKE_DETECTED:
        if (self && self->m_enabled)
        {
            self->m_animator.TriggerAnimation();
        }
        return 0;

    // ── 系统托盘图标回调 ────────────────────────────────────────────
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP && self)
        {
            POINT pt = {};
            GetCursorPos(&pt);

            // SetForegroundWindow 确保点击菜单外部时菜单能正确关闭
            SetForegroundWindow(hwnd);
            TrackPopupMenu(
                self->m_tray.GetMenu(),
                TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
                pt.x, pt.y,
                0, hwnd, nullptr);

            // 确保 TrackPopupMenu 后消息队列正确处理
            PostMessageW(hwnd, WM_NULL, 0, 0);
        }
        return 0;

    // ── 托盘菜单命令 ────────────────────────────────────────────────
    case WM_COMMAND:
        if (self)
        {
            switch (LOWORD(wParam))
            {
            case ID_TRAY_TOGGLE:
                self->SetEnabled(!self->m_enabled);
                break;

            case ID_TRAY_EXIT:
                self->Shutdown();
                PostQuitMessage(0);
                break;

            default:
                break;
            }
        }
        return 0;

    // ── 窗口销毁 ────────────────────────────────────────────────────
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
