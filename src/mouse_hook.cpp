#include "mouse_hook.h"
#include "config.h"
#include <cstdlib>

// 静态成员定义
MouseHook* MouseHook::s_instance = nullptr;

bool MouseHook::Install(HWND notifyWnd) {
    m_notify = notifyWnd;

    // 设置静态实例指针，使回调可访问 this
    s_instance = this;

    m_hook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
    return m_hook != nullptr;
}

void MouseHook::Uninstall() {
    if (m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

void MouseHook::SetEnabled(bool enabled) {
    m_enabled = enabled;
}

// 静态低级鼠标钩子回调
LRESULT CALLBACK MouseHook::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && s_instance != nullptr) {
        // 只处理鼠标移动消息
        if (wParam == WM_MOUSEMOVE) {
            if (s_instance->m_enabled) {
                const MSLLHOOKSTRUCT* pMouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
                POINT pos = pMouse->pt;
                DWORD now = pMouse->time;

                if (s_instance->CheckShake(pos, now)) {
                    // 检测到抖动，通知主窗口
                    PostMessage(s_instance->m_notify, WM_SHAKE_DETECTED, 0, 0);
                }
            }
        }
    }

    return CallNextHookEx(s_instance ? s_instance->m_hook : nullptr, nCode, wParam, lParam);
}

// 方向反转计数抖动检测算法（严格按设计文档实现）
bool MouseHook::CheckShake(POINT pos, DWORD now) {
    int dx = pos.x - m_lastX;

    // 移动量不足，忽略（过滤微小抖动噪声）
    if (abs(dx) < Config::MIN_MOVE_PIXELS) return false;

    int curDir = (dx > 0) ? 1 : -1;

    // 时间窗口超时：重置检测状态
    if (now - m_windowStart > Config::SHAKE_WINDOW_MS) {
        m_reversalCount = 0;
        m_windowStart   = now;
        m_lastDir       = curDir;
        m_lastX         = pos.x;
        return false;
    }

    // 检测方向反转
    if (m_lastDir != 0 && curDir != m_lastDir) {
        ++m_reversalCount;

        if (m_reversalCount >= Config::SHAKE_COUNT) {
            // 触发抖动检测，重置状态防止连续触发
            m_reversalCount = 0;
            m_windowStart   = now;
            m_lastDir       = curDir;
            m_lastX         = pos.x;
            return true;
        }
    }

    m_lastDir = curDir;
    m_lastX   = pos.x;
    return false;
}
