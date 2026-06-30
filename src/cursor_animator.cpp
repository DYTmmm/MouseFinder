#include "cursor_animator.h"
#include "cursor_scaler.h"
#include "config.h"

CursorAnimator* CursorAnimator::s_instance = nullptr;

void CursorAnimator::Initialize(CursorScaler* scaler, HWND notifyWnd)
{
    m_scaler    = scaler;
    m_notifyWnd = notifyWnd;
    s_instance  = this;

    CreateTimerQueueTimer(
        &m_timer, NULL, TimerCallback, nullptr,
        Config::TIMER_INTERVAL, Config::TIMER_INTERVAL,
        WT_EXECUTEDEFAULT);
}

void CursorAnimator::Shutdown()
{
    if (m_timer) {
        DeleteTimerQueueTimer(NULL, m_timer, NULL);
        m_timer = nullptr;
    }
    if (m_state != State::IDLE && m_scaler)
        m_scaler->RestoreCursors();

    m_state     = State::IDLE;
    m_factor    = 1.0f;
    m_startTime = 0;
    s_instance  = nullptr;
}

float CursorAnimator::EaseOut(float t)
{
    float inv   = 1.0f - t;
    float eased = 1.0f - inv * inv * inv;
    return Config::MAX_SCALE_FACTOR - eased * (Config::MAX_SCALE_FACTOR - 1.0f);
}

// ---------------------------------------------------------------------------
// TriggerAnimation
// 触发时先做一次性预计算（PrepareAnimation），成功后立即显示峰值帧
// ---------------------------------------------------------------------------
void CursorAnimator::TriggerAnimation()
{
    // 每次触发都重新预计算（确保拿到当前最新的光标样式）
    // PrepareAnimationAsync 只同步计算峰值帧（极快），其余帧后台异步完成
    if (!m_scaler->PrepareAnimationAsync()) {
        return;  // 极罕见失败，静默忽略
    }

    m_startTime = GetTickCount();
    m_factor    = Config::MAX_SCALE_FACTOR;
    m_state     = State::HOLDING;

    // 立即显示峰值帧（帧 0 已同步计算完毕）
    m_scaler->ApplyFrame(0);
}

// ---------------------------------------------------------------------------
// Tick — 主线程执行，动画阶段直接查帧缓存，零计算开销
// ---------------------------------------------------------------------------
void CursorAnimator::Tick()
{
    if (m_state == State::IDLE) return;

    DWORD elapsed = GetTickCount() - m_startTime;

    if (m_state == State::HOLDING)
    {
        if (elapsed >= Config::HOLD_DURATION) {
            m_startTime = GetTickCount();
            m_state     = State::SHRINKING;
        }
        return;
    }

    // SHRINKING
    elapsed = GetTickCount() - m_startTime;
    float t = static_cast<float>(elapsed) / static_cast<float>(Config::SHRINK_DURATION);

    if (t >= 1.0f) {
        m_factor = 1.0f;
        m_state  = State::IDLE;
        m_scaler->RestoreCursors();  // 同时清空帧缓存，下次触发重新预计算
        return;
    }

    m_factor = EaseOut(t);

    // 查表：把 factor 映射到帧索引，直接 SetSystemCursor，无任何像素运算
    int frameIdx = m_scaler->FactorToFrame(m_factor);
    m_scaler->ApplyFrame(frameIdx);
}

bool CursorAnimator::IsAnimating() const
{
    return m_state != State::IDLE;
}

VOID CALLBACK CursorAnimator::TimerCallback(PVOID, BOOLEAN)
{
    CursorAnimator* self = s_instance;
    if (self && self->m_notifyWnd)
        PostMessageW(self->m_notifyWnd, WM_ANIM_TICK, 0, 0);
}
