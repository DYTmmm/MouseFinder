#include "cursor_animator.h"
#include "cursor_scaler.h"
#include "config.h"

// 静态成员定义
CursorAnimator* CursorAnimator::s_instance = nullptr;

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------
void CursorAnimator::Initialize(CursorScaler* scaler, HWND notifyWnd)
{
    m_scaler    = scaler;
    m_notifyWnd = notifyWnd;
    s_instance  = this;

    // 定时器回调只做 PostMessage，不直接操作光标，避免线程竞争
    CreateTimerQueueTimer(
        &m_timer,
        NULL,
        TimerCallback,
        nullptr,
        Config::TIMER_INTERVAL,
        Config::TIMER_INTERVAL,
        WT_EXECUTEDEFAULT
    );
}

// ---------------------------------------------------------------------------
// Shutdown
// ---------------------------------------------------------------------------
void CursorAnimator::Shutdown()
{
    if (m_timer != nullptr)
    {
        // NULL 作为完成事件：不阻塞等待（避免在主线程 Shutdown 时死锁）
        DeleteTimerQueueTimer(NULL, m_timer, NULL);
        m_timer = nullptr;
    }

    if (m_state != State::IDLE && m_scaler != nullptr)
    {
        m_scaler->RestoreCursors();
    }

    m_state     = State::IDLE;
    m_factor    = 1.0f;
    m_startTime = 0;
    s_instance  = nullptr;
}

// ---------------------------------------------------------------------------
// EaseOut — 缓出三次方，起始段慢、结尾收缩自然
// ---------------------------------------------------------------------------
float CursorAnimator::EaseOut(float t)
{
    float inv   = 1.0f - t;
    float eased = 1.0f - inv * inv * inv;
    return Config::MAX_SCALE_FACTOR - eased * (Config::MAX_SCALE_FACTOR - 1.0f);
}

// ---------------------------------------------------------------------------
// TriggerAnimation — 在主线程调用，立即应用峰值，进入 HOLDING
// ---------------------------------------------------------------------------
void CursorAnimator::TriggerAnimation()
{
    m_startTime = GetTickCount();
    m_factor    = Config::MAX_SCALE_FACTOR;
    m_state     = State::HOLDING;
    m_scaler->ApplyScale(Config::MAX_SCALE_FACTOR);
}

// ---------------------------------------------------------------------------
// Tick — 由主线程消息循环响应 WM_ANIM_TICK 调用
// 所有状态读写和 SetSystemCursor 调用都在主线程，无竞争
// ---------------------------------------------------------------------------
void CursorAnimator::Tick()
{
    if (m_state == State::IDLE) return;

    DWORD elapsed = GetTickCount() - m_startTime;

    if (m_state == State::HOLDING)
    {
        if (elapsed >= Config::HOLD_DURATION)
        {
            m_startTime = GetTickCount();
            m_state     = State::SHRINKING;
        }
        return;  // HOLDING 阶段不更新光标
    }

    // SHRINKING 阶段
    elapsed = GetTickCount() - m_startTime;
    float t = static_cast<float>(elapsed) / static_cast<float>(Config::SHRINK_DURATION);

    if (t >= 1.0f)
    {
        m_factor = 1.0f;
        m_state  = State::IDLE;
        m_scaler->RestoreCursors();
        return;
    }

    m_factor = EaseOut(t);
    m_scaler->ApplyScale(m_factor);
}

// ---------------------------------------------------------------------------
// IsAnimating
// ---------------------------------------------------------------------------
bool CursorAnimator::IsAnimating() const
{
    return m_state != State::IDLE;
}

// ---------------------------------------------------------------------------
// TimerCallback — 线程池线程调用，只 PostMessage，不接触任何共享状态
// ---------------------------------------------------------------------------
VOID CALLBACK CursorAnimator::TimerCallback(PVOID /*lpParameter*/, BOOLEAN /*timerOrWaitFired*/)
{
    CursorAnimator* self = s_instance;
    if (self && self->m_notifyWnd)
    {
        // PostMessage 是线程安全的，投递到主线程消息队列后立即返回
        PostMessageW(self->m_notifyWnd, WM_ANIM_TICK, 0, 0);
    }
}
