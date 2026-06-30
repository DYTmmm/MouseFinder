#include "cursor_scaler.h"
#include "config.h"
#include <cstring>
#include <cmath>

// 注册表光标名称映射
struct CursorRegEntry { DWORD id; const wchar_t* regName; };
static const CursorRegEntry kCursorMap[] = {
    { 32512, L"Arrow"       },
    { 32513, L"IBeam"       },
    { 32514, L"Wait"        },
    { 32515, L"Crosshair"   },
    { 32516, L"UpArrow"     },
    { 32642, L"SizeNWSE"    },
    { 32643, L"SizeNESW"    },
    { 32644, L"SizeWE"      },
    { 32645, L"SizeNS"      },
    { 32646, L"SizeAll"     },
    { 32648, L"No"          },
    { 32649, L"Hand"        },
    { 32650, L"AppStarting" },
};
static constexpr int kCursorCount = static_cast<int>(sizeof(kCursorMap) / sizeof(kCursorMap[0]));

// ---------------------------------------------------------------------------
// LoadRealCursor
// 统一以 SM_CXCURSOR/SM_CYCURSOR（系统当前光标显示尺寸）为基准加载光标。
// 无论注册表指向的文件有多大（32px/128px/256px），最终都缩放到该基准尺寸。
// 这样 MAX_SCALE_FACTOR 倍就是相对于"屏幕上实际显示大小"的 5 倍，始终一致。
// ---------------------------------------------------------------------------
HCURSOR CursorScaler::LoadRealCursor(DWORD cursorId) const {
    // 系统当前光标基准尺寸（用户在鼠标设置里选的大小：32/48/64/96px）
    int cx = GetSystemMetrics(SM_CXCURSOR);
    int cy = GetSystemMetrics(SM_CYCURSOR);

    // 找注册表值名
    const wchar_t* regName = nullptr;
    for (int i = 0; i < kCursorCount; ++i) {
        if (kCursorMap[i].id == cursorId) { regName = kCursorMap[i].regName; break; }
    }

    if (regName) {
        HKEY hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Cursors",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t path[MAX_PATH] = {};
            DWORD   sz   = sizeof(path);
            DWORD   type = 0;
            LONG    res  = RegQueryValueExW(hKey, regName, nullptr, &type,
                                            reinterpret_cast<LPBYTE>(path), &sz);
            RegCloseKey(hKey);

            if (res == ERROR_SUCCESS && path[0] != L'\0') {
                wchar_t expanded[MAX_PATH] = {};
                ExpandEnvironmentStringsW(path, expanded, MAX_PATH);

                // 明确指定 cx/cy：把文件里的光标缩放到系统基准尺寸
                // 无论原文件是 256px 还是 32px，加载出来的都是 cx×cy
                HCURSOR h = reinterpret_cast<HCURSOR>(
                    LoadImageW(nullptr, expanded, IMAGE_CURSOR,
                               cx, cy, LR_LOADFROMFILE));
                if (h) return h;
            }
        }
    }

    // 注册表为空或文件加载失败：加载系统内置光标（按基准尺寸）
    HCURSOR hBase = reinterpret_cast<HCURSOR>(
        LoadImageW(nullptr, MAKEINTRESOURCEW(cursorId), IMAGE_CURSOR,
                   cx, cy, LR_SHARED | LR_DEFAULTCOLOR));
    if (!hBase) {
        hBase = LoadCursor(nullptr, MAKEINTRESOURCEW(cursorId));
    }
    if (!hBase) return nullptr;

    // LR_SHARED 的句柄不能 DestroyCursor，用 CopyImage 创建独立副本（同时确保尺寸）
    return reinterpret_cast<HCURSOR>(
        CopyImage(hBase, IMAGE_CURSOR, cx, cy, 0));
}

// ---------------------------------------------------------------------------
// ScaleCursorToSize — DIB 双线性插值缩放
// ---------------------------------------------------------------------------
HCURSOR CursorScaler::ScaleCursorToSize(const CursorInfo& ci,
                                         int targetW, int targetH,
                                         int hotX, int hotY) const {
    if (!ci.orig || ci.w <= 0 || ci.h <= 0 || targetW <= 0 || targetH <= 0)
        return nullptr;

    ICONINFO ii = {};
    if (!GetIconInfo(ci.orig, &ii)) return nullptr;

    HBITMAP hNewColor = nullptr;
    HBITMAP hNewMask  = nullptr;

    if (ii.hbmColor) {
        // ── 彩色光标 DIB 路径 ─────────────────────────────────────────
        BITMAPINFOHEADER bih = {};
        bih.biSize = sizeof(bih); bih.biWidth = ci.w; bih.biHeight = -ci.h;
        bih.biPlanes = 1; bih.biBitCount = 32; bih.biCompression = BI_RGB;

        int srcStride = ci.w * 4;
        std::vector<BYTE> src(srcStride * ci.h, 0);

        HDC hdcTmp = GetDC(nullptr);
        bool ok = GetDIBits(hdcTmp, ii.hbmColor, 0, ci.h, src.data(),
                            reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS) != 0;
        if (!ok) {
            ReleaseDC(nullptr, hdcTmp);
            DeleteObject(ii.hbmMask); DeleteObject(ii.hbmColor);
            return nullptr;
        }

        int dstStride = targetW * 4;
        std::vector<BYTE> dst(dstStride * targetH, 0);

        float sx = static_cast<float>(ci.w)  / static_cast<float>(targetW);
        float sy = static_cast<float>(ci.h) / static_cast<float>(targetH);

        for (int dy = 0; dy < targetH; ++dy) {
            float fy0 = (dy + 0.5f) * sy - 0.5f;
            int   y0  = static_cast<int>(fy0);
            int   y1  = y0 + 1;
            float wy  = fy0 - static_cast<float>(y0);
            if (y0 < 0) y0 = 0; if (y1 >= ci.h) y1 = ci.h - 1;

            for (int dx = 0; dx < targetW; ++dx) {
                float fx0 = (dx + 0.5f) * sx - 0.5f;
                int   x0  = static_cast<int>(fx0);
                int   x1  = x0 + 1;
                float wx  = fx0 - static_cast<float>(x0);
                if (x0 < 0) x0 = 0; if (x1 >= ci.w) x1 = ci.w - 1;

                const BYTE* p00 = &src[y0 * srcStride + x0 * 4];
                const BYTE* p10 = &src[y0 * srcStride + x1 * 4];
                const BYTE* p01 = &src[y1 * srcStride + x0 * 4];
                const BYTE* p11 = &src[y1 * srcStride + x1 * 4];
                BYTE*       out = &dst[dy * dstStride + dx * 4];

                for (int c = 0; c < 4; ++c) {
                    float v = p00[c] * (1.f - wx) * (1.f - wy)
                            + p10[c] *        wx  * (1.f - wy)
                            + p01[c] * (1.f - wx) *        wy
                            + p11[c] *        wx  *        wy;
                    out[c] = static_cast<BYTE>(v + 0.5f);
                }
            }
        }

        // 目标 DIB
        BITMAPINFOHEADER bihDst = {};
        bihDst.biSize = sizeof(bihDst); bihDst.biWidth = targetW;
        bihDst.biHeight = -targetH; bihDst.biPlanes = 1;
        bihDst.biBitCount = 32; bihDst.biCompression = BI_RGB;
        void* pBits = nullptr;
        hNewColor = CreateDIBSection(hdcTmp,
                                     reinterpret_cast<BITMAPINFO*>(&bihDst),
                                     DIB_RGB_COLORS, &pBits, nullptr, 0);
        if (hNewColor && pBits) memcpy(pBits, dst.data(), dstStride * targetH);
        ReleaseDC(nullptr, hdcTmp);

        // AND mask from alpha
        hNewMask = CreateBitmap(targetW, targetH, 1, 1, nullptr);
        if (hNewMask) {
            int ms = ((targetW + 15) / 16) * 2;
            std::vector<BYTE> mb(ms * targetH, 0);
            for (int my = 0; my < targetH; ++my)
                for (int mx = 0; mx < targetW; ++mx)
                    if (dst[my * dstStride + mx * 4 + 3] == 0)
                        mb[my * ms + mx / 8] |= (0x80 >> (mx % 8));
            SetBitmapBits(hNewMask, static_cast<DWORD>(mb.size()), mb.data());
        }

    } else {
        // ── 单色光标 ─────────────────────────────────────────────────────
        int mH  = ci.h * 2;
        int nmH = targetH * 2;
        hNewMask = CreateBitmap(targetW, nmH, 1, 1, nullptr);
        HDC s = CreateCompatibleDC(nullptr), d = CreateCompatibleDC(nullptr);
        HGDIOBJ os = SelectObject(s, ii.hbmMask);
        HGDIOBJ od = SelectObject(d, hNewMask);
        SetStretchBltMode(d, COLORONCOLOR);
        StretchBlt(d, 0, 0, targetW, nmH, s, 0, 0, ci.w, mH, SRCCOPY);
        SelectObject(s, os); SelectObject(d, od);
        DeleteDC(s); DeleteDC(d);
    }

    HCURSOR result = nullptr;
    if (hNewMask) {
        ICONINFO ni = {};
        ni.fIcon = FALSE;
        ni.xHotspot = static_cast<DWORD>(hotX);
        ni.yHotspot = static_cast<DWORD>(hotY);
        ni.hbmMask  = hNewMask;
        ni.hbmColor = hNewColor;
        result = reinterpret_cast<HCURSOR>(CreateIconIndirect(&ni));
    }

    DeleteObject(ii.hbmMask);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (hNewMask)    DeleteObject(hNewMask);
    if (hNewColor)   DeleteObject(hNewColor);
    return result;
}

// ---------------------------------------------------------------------------
// ComputeFrame — 计算单帧（线程安全，可在后台线程调用）
// ---------------------------------------------------------------------------
void CursorScaler::ComputeFrame(int f) {
    float t      = static_cast<float>(f) / static_cast<float>(ANIM_FRAMES - 1);
    float inv    = 1.0f - t;
    float eased  = 1.0f - inv * inv * inv;
    float factor = Config::MAX_SCALE_FACTOR - eased * (Config::MAX_SCALE_FACTOR - 1.0f);

    m_frames[f].factor = factor;
    m_frames[f].cursors.resize(m_infos.size(), nullptr);

    for (size_t i = 0; i < m_infos.size(); ++i) {
        const CursorInfo& ci = m_infos[i];
        int tw = static_cast<int>(ci.w * factor + 0.5f);
        int th = static_cast<int>(ci.h * factor + 0.5f);
        int hx = static_cast<int>(ci.hotX * factor + 0.5f);
        int hy = static_cast<int>(ci.hotY * factor + 0.5f);
        if (tw < 1) tw = 1; if (th < 1) th = 1;
        m_frames[f].cursors[i] = ScaleCursorToSize(ci, tw, th, hx, hy);
    }
    m_frames[f].ready = true;
}

// ---------------------------------------------------------------------------
// ClearFrameCache
// ---------------------------------------------------------------------------
void CursorScaler::ClearFrameCache() {
    for (int f = 0; f < ANIM_FRAMES; ++f) {
        for (HCURSOR h : m_frames[f].cursors) if (h) DestroyCursor(h);
        m_frames[f].cursors.clear();
        m_frames[f].ready = false;
    }
    for (auto& ci : m_infos) if (ci.orig) DestroyCursor(ci.orig);
    m_infos.clear();
}

// ---------------------------------------------------------------------------
// WorkerThread — 后台线程：从帧 1 开始逐帧计算
// ---------------------------------------------------------------------------
DWORD WINAPI CursorScaler::WorkerThread(LPVOID param) {
    CursorScaler* self = reinterpret_cast<CursorScaler*>(param);
    for (int f = 1; f < ANIM_FRAMES; ++f) {
        if (self->m_stopWorker.load()) break;
        self->ComputeFrame(f);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// PrepareAnimationAsync
// 主线程：同步计算帧 0（峰值），立即可用；启动后台线程计算帧 1..N-1
// ---------------------------------------------------------------------------
bool CursorScaler::PrepareAnimationAsync() {
    // 等待上一次后台线程结束
    if (m_workerThread) {
        m_stopWorker.store(true);
        WaitForSingleObject(m_workerThread, 2000);
        CloseHandle(m_workerThread);
        m_workerThread = nullptr;
    }
    m_stopWorker.store(false);
    ClearFrameCache();

    // ── 加载所有光标的真实尺寸句柄 ──────────────────────────────────────
    for (int i = 0; i < kCursorCount; ++i) {
        HCURSOR h = LoadRealCursor(kCursorMap[i].id);
        if (!h) continue;

        ICONINFO ii = {};
        if (!GetIconInfo(h, &ii)) { DestroyCursor(h); continue; }
        BITMAP bm = {};
        GetObject(ii.hbmMask, sizeof(bm), &bm);
        bool hasColor = (ii.hbmColor != nullptr);

        CursorInfo ci;
        ci.id   = kCursorMap[i].id;
        ci.w    = bm.bmWidth;
        ci.h    = hasColor ? bm.bmHeight : bm.bmHeight / 2;
        ci.hotX = static_cast<int>(ii.xHotspot);
        ci.hotY = static_cast<int>(ii.yHotspot);
        ci.orig = h;
        DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        m_infos.push_back(ci);
    }
    if (m_infos.empty()) return false;

    // ── 同步计算帧 0（峰值），主线程立即可用 ────────────────────────────
    ComputeFrame(0);
    m_peakReady.store(true);

    // ── 后台线程计算帧 1..N-1 ───────────────────────────────────────────
    m_workerThread = CreateThread(nullptr, 0, WorkerThread, this, 0, nullptr);

    return true;
}

// ---------------------------------------------------------------------------
// FactorToFrame
// ---------------------------------------------------------------------------
int CursorScaler::FactorToFrame(float factor) const {
    int best = 0;
    float bestDiff = fabsf(m_frames[0].factor - factor);
    for (int i = 1; i < ANIM_FRAMES; ++i) {
        if (!m_frames[i].ready) break;  // 后台线程尚未计算到此帧
        float d = fabsf(m_frames[i].factor - factor);
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// ApplyFrame
// ---------------------------------------------------------------------------
void CursorScaler::ApplyFrame(int frameIndex) {
    if (frameIndex < 0 || frameIndex >= ANIM_FRAMES) return;
    if (!m_frames[frameIndex].ready) return;

    const AnimFrame& frame = m_frames[frameIndex];
    for (size_t i = 0; i < m_infos.size(); ++i) {
        if (i >= frame.cursors.size() || !frame.cursors[i]) continue;
        // CopyImage 创建副本给 SetSystemCursor（会销毁句柄）
        HCURSOR hCopy = reinterpret_cast<HCURSOR>(
            CopyImage(frame.cursors[i], IMAGE_CURSOR, 0, 0, 0));
        if (!hCopy) continue;
        if (!SetSystemCursor(hCopy, m_infos[i].id)) {
            OutputDebugStringW(L"CursorScaler::ApplyFrame: SetSystemCursor failed\n");
            DestroyCursor(hCopy);
        }
    }
}

// ---------------------------------------------------------------------------
// RestoreCursors
// ---------------------------------------------------------------------------
void CursorScaler::RestoreCursors() {
    // 停止后台线程
    if (m_workerThread) {
        m_stopWorker.store(true);
        WaitForSingleObject(m_workerThread, 2000);
        CloseHandle(m_workerThread);
        m_workerThread = nullptr;
    }
    m_peakReady.store(false);
    SystemParametersInfo(SPI_SETCURSORS, 0, nullptr, 0);
    ClearFrameCache();
}
