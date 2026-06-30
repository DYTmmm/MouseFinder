#pragma once

#include <windows.h>

// 前向声明，避免循环依赖
class CursorScaler;

// 动画定时器 tick 消息，由定时器线程 PostMessage 到主窗口，主线程处理
#define WM_ANIM_TICK  (WM_USER + 101)

// CursorAnimator — 光标动画模块
// 管理动画状态机（IDLE / HOLDING / SHRINKING），维护 TimerQueue 定时器。
// 定时器回调只 PostMessage，所有光标操作在主线程执行，避免多线程竞争导致抖动。
class CursorAnimator {
public:
    // 初始化：保存 CursorScaler 指针和通知窗口句柄，创建定时器
    void Initialize(CursorScaler* scaler, HWND notifyWnd);

    // 关闭：删除定时器，若动画中则恢复光标，重置所有状态
    void Shutdown();

    // 触发（或重入）动画：立即放大到峰值，进入 HOLDING 阶段
    void TriggerAnimation();

    // 由主线程消息循环调用（响应 WM_ANIM_TICK）：推进动画状态机
    void Tick();

    // 返回当前是否处于动画状态
    bool IsAnimating() const;

    // 缓出三次方缓动函数，公开以方便测试
    static float EaseOut(float t);

private:
    // 三段式状态：IDLE → HOLDING（保持峰值）→ SHRINKING（缩小）
    enum class State { IDLE, HOLDING, SHRINKING };

    State         m_state     = State::IDLE;
    float         m_factor    = 1.0f;
    DWORD         m_startTime = 0;
    CursorScaler* m_scaler    = nullptr;
    HWND          m_notifyWnd = nullptr;  // 接收 WM_ANIM_TICK 的主窗口
    HANDLE        m_timer     = nullptr;

    static CursorAnimator* s_instance;
    static VOID CALLBACK TimerCallback(PVOID lpParameter, BOOLEAN timerOrWaitFired);
};
