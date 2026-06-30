#pragma once

#include <windows.h>
#include <vector>

// CursorScaler — 光标缩放引擎
// 负责捕获系统光标原始句柄、对位图执行 GDI 缩放，并通过 SetSystemCursor 替换系统光标。
class CursorScaler {
public:
    // 程序启动时调用，快照所有标准系统光标的原始句柄副本（CopyImage）。
    // 返回 true 表示至少成功捕获了一个光标。
    bool CaptureCurrentCursors();

    // 将每个已捕获的标准光标缩放到 factor 倍并替换系统光标。
    // 若 SetSystemCursor 失败，静默记录调试信息并继续处理其余光标。
    void ApplyScale(float factor);

    // 通过 SystemParametersInfo(SPI_SETCURSORS) 将所有系统光标恢复为默认值。
    void RestoreCursors();

private:
    struct CursorEntry {
        DWORD   cursorId;   // 系统光标 ID，如 OCR_NORMAL(32512)
        HCURSOR original;   // CopyImage 副本，由本类负责 DestroyCursor
    };

    std::vector<CursorEntry> m_cursors;

    // 将单个光标 src 缩放 factor 倍，返回新的 HCURSOR。
    // 调用方（即 ApplyScale / SetSystemCursor）获取句柄所有权；
    // 若缩放过程出错则返回 NULL。
    HCURSOR ScaleCursor(HCURSOR src, float factor);
};
