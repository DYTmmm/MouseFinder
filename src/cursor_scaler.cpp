#include "cursor_scaler.h"
#include <cstring>  // memcpy

// 标准系统光标 ID 列表（winuser.h 中定义）
static const DWORD kStandardCursorIds[] = {
    32512, // OCR_NORMAL      — 默认箭头
    32513, // OCR_IBEAM       — 文本 I 型光标
    32514, // OCR_WAIT        — 等待（沙漏/圆圈）
    32515, // OCR_CROSS       — 十字光标
    32516, // OCR_UP          — 向上箭头
    32642, // OCR_SIZENWSE    — 左上-右下双向调整
    32643, // OCR_SIZENESW    — 右上-左下双向调整
    32644, // OCR_SIZEWE      — 左右双向调整
    32645, // OCR_SIZENS      — 上下双向调整
    32646, // OCR_SIZEALL     — 四向移动
    32648, // OCR_NO          — 禁止
    32649, // OCR_HAND        — 手型（链接）
    32650, // OCR_APPSTARTING — 应用启动中（箭头+沙漏）
};

// ---------------------------------------------------------------------------
// CaptureCurrentCursors
// ---------------------------------------------------------------------------
bool CursorScaler::CaptureCurrentCursors() {
    for (auto& entry : m_cursors) {
        if (entry.original) DestroyCursor(entry.original);
    }
    m_cursors.clear();

    for (DWORD id : kStandardCursorIds) {
        HCURSOR hBase = LoadCursor(NULL, MAKEINTRESOURCE(id));
        if (!hBase) continue;

        HCURSOR hCopy = reinterpret_cast<HCURSOR>(
            CopyImage(hBase, IMAGE_CURSOR, 0, 0, LR_DEFAULTSIZE));
        if (!hCopy) continue;

        m_cursors.push_back({id, hCopy});
    }

    return !m_cursors.empty();
}

// ---------------------------------------------------------------------------
// ScaleCursor — 用 DIB 直接操作像素，正确处理 32bpp alpha 通道
//
// 思路：
//   彩色光标（hbmColor 存在）全部用 DIB 路径：
//     1. 把 hbmColor 读取为 32bpp BGRA 像素数组
//     2. 用双线性插值缩放到目标尺寸
//     3. 重新写回新 DIB，生成新 hbmColor
//     4. AND mask 设为全黑（0x00）：当 AND=0 时系统完全依赖 Color 位图的 alpha 通道
//
//   单色光标（hbmColor == NULL）走 GDI 路径，用 COLORONCOLOR 缩放 mask。
// ---------------------------------------------------------------------------
HCURSOR CursorScaler::ScaleCursor(HCURSOR src, float factor) {
    if (!src || factor <= 0.0f) return NULL;

    ICONINFO ii = {};
    if (!GetIconInfo(src, &ii)) return NULL;

    // 获取原始尺寸（从 mask 位图读）
    BITMAP bm = {};
    if (!GetObject(ii.hbmMask, sizeof(bm), &bm)) {
        DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        return NULL;
    }

    int origW = bm.bmWidth;
    int origH = (ii.hbmColor != NULL) ? bm.bmHeight : bm.bmHeight / 2;
    int newW  = static_cast<int>(origW * factor);
    int newH  = static_cast<int>(origH * factor);

    if (newW <= 0 || newH <= 0) {
        DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        return NULL;
    }

    HBITMAP hNewColor = NULL;
    HBITMAP hNewMask  = NULL;

    if (ii.hbmColor) {
        // ── 彩色光标：DIB 路径 ──────────────────────────────────────────

        // 1. 读取原始 Color 位图像素（32bpp BGRA）
        BITMAPINFOHEADER bih = {};
        bih.biSize        = sizeof(BITMAPINFOHEADER);
        bih.biWidth       = origW;
        bih.biHeight      = -origH;  // 负数 = 从上到下（top-down）
        bih.biPlanes      = 1;
        bih.biBitCount    = 32;
        bih.biCompression = BI_RGB;

        int srcStride = origW * 4;
        std::vector<BYTE> srcPixels(srcStride * origH, 0);

        HDC hdcTmp = GetDC(NULL);
        if (!GetDIBits(hdcTmp, ii.hbmColor, 0, origH,
                       srcPixels.data(),
                       reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS)) {
            ReleaseDC(NULL, hdcTmp);
            DeleteObject(ii.hbmMask);
            DeleteObject(ii.hbmColor);
            return NULL;
        }

        // 2. 分配目标像素缓冲
        int dstStride = newW * 4;
        std::vector<BYTE> dstPixels(dstStride * newH, 0);

        // 3. 双线性插值缩放（正确处理 pre-multiplied alpha）
        float scaleX = static_cast<float>(origW) / static_cast<float>(newW);
        float scaleY = static_cast<float>(origH) / static_cast<float>(newH);

        for (int dy = 0; dy < newH; ++dy) {
            float sy = (dy + 0.5f) * scaleY - 0.5f;
            int   sy0 = static_cast<int>(sy);
            int   sy1 = sy0 + 1;
            float fy  = sy - sy0;
            if (sy0 < 0)       { sy0 = 0; }
            if (sy1 >= origH)  { sy1 = origH - 1; }

            for (int dx = 0; dx < newW; ++dx) {
                float sx = (dx + 0.5f) * scaleX - 0.5f;
                int   sx0 = static_cast<int>(sx);
                int   sx1 = sx0 + 1;
                float fx  = sx - sx0;
                if (sx0 < 0)       { sx0 = 0; }
                if (sx1 >= origW)  { sx1 = origW - 1; }

                // 四个相邻像素（BGRA）
                const BYTE* p00 = &srcPixels[sy0 * srcStride + sx0 * 4];
                const BYTE* p10 = &srcPixels[sy0 * srcStride + sx1 * 4];
                const BYTE* p01 = &srcPixels[sy1 * srcStride + sx0 * 4];
                const BYTE* p11 = &srcPixels[sy1 * srcStride + sx1 * 4];

                BYTE* dst = &dstPixels[dy * dstStride + dx * 4];

                // 对 BGRA 四个通道各做双线性插值
                for (int c = 0; c < 4; ++c) {
                    float v = p00[c] * (1.0f - fx) * (1.0f - fy)
                            + p10[c] *         fx  * (1.0f - fy)
                            + p01[c] * (1.0f - fx) *         fy
                            + p11[c] *         fx  *         fy;
                    dst[c] = static_cast<BYTE>(v + 0.5f);
                }
            }
        }

        // 4. 创建目标 DIB
        BITMAPINFOHEADER bihDst = {};
        bihDst.biSize        = sizeof(BITMAPINFOHEADER);
        bihDst.biWidth       = newW;
        bihDst.biHeight      = -newH;
        bihDst.biPlanes      = 1;
        bihDst.biBitCount    = 32;
        bihDst.biCompression = BI_RGB;

        void* pBits = nullptr;
        hNewColor = CreateDIBSection(hdcTmp,
                                     reinterpret_cast<BITMAPINFO*>(&bihDst),
                                     DIB_RGB_COLORS, &pBits, NULL, 0);
        if (hNewColor && pBits) {
            memcpy(pBits, dstPixels.data(), dstStride * newH);
        }
        ReleaseDC(NULL, hdcTmp);

        // 5. 根据 alpha 通道生成 AND mask
        //    alpha == 0 → mask bit = 1（透明，不显示）
        //    alpha >  0 → mask bit = 0（不透明，显示 Color 位图颜色）
        //    这样边缘半透明像素的 alpha 渐变信息保留在 Color 位图里，
        //    mask 只做"完全透明 vs 其他"的区分，消除黑边。
        hNewMask = CreateBitmap(newW, newH, 1, 1, NULL);
        if (hNewMask) {
            // 把每行的 alpha 通道转为 1bpp mask 位图
            // mask 行步长必须对齐到 WORD（2字节）
            int maskStride = ((newW + 15) / 16) * 2;
            std::vector<BYTE> maskBits(maskStride * newH, 0x00);

            for (int my = 0; my < newH; ++my) {
                for (int mx = 0; mx < newW; ++mx) {
                    BYTE alpha = dstPixels[my * dstStride + mx * 4 + 3];
                    if (alpha == 0) {
                        // 完全透明：mask bit = 1
                        maskBits[my * maskStride + mx / 8] |= (0x80 >> (mx % 8));
                    }
                    // alpha > 0：mask bit 保持 0（不透明）
                }
            }
            SetBitmapBits(hNewMask, static_cast<DWORD>(maskBits.size()), maskBits.data());
        }

    } else {
        // ── 单色光标：GDI 路径，COLORONCOLOR 缩放 mask ──────────────────
        int maskH    = origH * 2;
        int newMaskH = newH  * 2;

        hNewMask = CreateBitmap(newW, newMaskH, 1, 1, NULL);

        HDC hdcSrc = CreateCompatibleDC(NULL);
        HDC hdcDst = CreateCompatibleDC(NULL);
        HGDIOBJ hOldSrc = SelectObject(hdcSrc, ii.hbmMask);
        HGDIOBJ hOldDst = SelectObject(hdcDst, hNewMask);

        SetStretchBltMode(hdcDst, COLORONCOLOR);
        StretchBlt(hdcDst, 0, 0, newW, newMaskH,
                   hdcSrc, 0, 0, origW, maskH, SRCCOPY);

        SelectObject(hdcSrc, hOldSrc);
        SelectObject(hdcDst, hOldDst);
        DeleteDC(hdcSrc);
        DeleteDC(hdcDst);
    }

    // ── 重建光标 ─────────────────────────────────────────────────────────
    HCURSOR result = NULL;
    if (hNewMask) {
        ICONINFO newII   = {};
        newII.fIcon      = FALSE;
        newII.xHotspot   = static_cast<DWORD>(ii.xHotspot * factor);
        newII.yHotspot   = static_cast<DWORD>(ii.yHotspot * factor);
        newII.hbmMask    = hNewMask;
        newII.hbmColor   = hNewColor;
        result = reinterpret_cast<HCURSOR>(CreateIconIndirect(&newII));
    }

    // ── 清理 ─────────────────────────────────────────────────────────────
    DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (hNewMask)    DeleteObject(hNewMask);
    if (hNewColor)   DeleteObject(hNewColor);

    return result;
}

// ---------------------------------------------------------------------------
// ApplyScale
// ---------------------------------------------------------------------------
void CursorScaler::ApplyScale(float factor) {
    for (const auto& entry : m_cursors) {
        if (!entry.original) continue;

        HCURSOR hScaled = ScaleCursor(entry.original, factor);
        if (!hScaled) continue;

        if (!SetSystemCursor(hScaled, entry.cursorId)) {
            OutputDebugStringW(L"CursorScaler::ApplyScale: SetSystemCursor failed\n");
        }
        // SetSystemCursor 获取所有权，无需 DestroyCursor
    }
}

// ---------------------------------------------------------------------------
// RestoreCursors
// ---------------------------------------------------------------------------
void CursorScaler::RestoreCursors() {
    SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0);
}
