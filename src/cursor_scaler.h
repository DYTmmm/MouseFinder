#pragma once

#include <windows.h>
#include <vector>
#include <atomic>

// CursorScaler — 方案 C 优化版
//
// 改进点：
//   1. 系统默认光标：用 SM_CXCURSOR/SM_CYCURSOR + LoadImage 加载真实系统尺寸
//   2. 预计算在后台线程执行，第 0 帧（峰值）同步计算后立即显示，
//      其余帧异步补全，动画期间按需查表
//   3. ApplyFrame 用原始 HCURSOR 副本传给 SetSystemCursor，无额外 CopyImage

class CursorScaler {
public:
    // 同步计算峰值帧（帧 0），立即可用；其余帧在后台线程异步计算
    // 触发时调用，执行极快（只计算 1 帧），不阻塞主线程
    bool PrepareAnimationAsync();

    // 按帧索引应用光标；若该帧尚未计算完成，使用已有最近帧
    void ApplyFrame(int frameIndex);

    // 把 factor 映射到帧索引
    int FactorToFrame(float factor) const;

    // 恢复系统光标，等待后台线程结束，清理所有缓存
    void RestoreCursors();

    // 总帧数
    int GetFrameCount() const { return ANIM_FRAMES; }

    // 峰值帧是否准备好（同步完成）
    bool IsReady() const { return m_peakReady.load(); }

    static constexpr int ANIM_FRAMES = 60;

private:
    struct CursorInfo {
        DWORD id;
        int   w, h;
        int   hotX, hotY;
        HCURSOR orig;   // 真实尺寸原始句柄
    };

    struct AnimFrame {
        float   factor  = 0.0f;
        bool    ready   = false;
        std::vector<HCURSOR> cursors;
    };

    std::vector<CursorInfo> m_infos;
    AnimFrame               m_frames[ANIM_FRAMES];
    std::atomic<bool>       m_peakReady   { false };
    std::atomic<bool>       m_stopWorker  { false };
    HANDLE                  m_workerThread{ nullptr };

    // 加载单个光标的真实尺寸句柄
    HCURSOR LoadRealCursor(DWORD cursorId) const;

    // 把 src 缩放到 targetW×targetH
    HCURSOR ScaleCursorToSize(const CursorInfo& ci,
                               int targetW, int targetH,
                               int hotX, int hotY) const;

    // 计算单帧（可在任何线程调用）
    void ComputeFrame(int frameIndex);

    // 清理所有帧缓存
    void ClearFrameCache();

    // 后台线程入口
    static DWORD WINAPI WorkerThread(LPVOID param);
};
