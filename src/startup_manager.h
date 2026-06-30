#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// StartupManager — 开机自启模块
// 职责：
//   - 读写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run
//   - 记录是否已询问过（HKCU\Software\MouseFinder\AskDone）
//   - 提供简单询问对话框（Win32 MessageBox）
class StartupManager {
public:
    // 若从未询问过用户，弹出对话框询问是否设置开机自启。
    // 询问结果记录到注册表，下次启动不再弹框。
    void CheckAndPrompt(HWND parent);

    // 检测 HKCU\...\Run\MouseFinder 是否存在
    bool IsRegistered() const;

    // enable=true：写入 exe 路径到 Run 项；enable=false：删除 Run 项
    void SetRegistered(bool enable);

private:
    static constexpr wchar_t REG_RUN[]    = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
    static constexpr wchar_t REG_CFG[]    = LR"(Software\MouseFinder)";
    static constexpr wchar_t VALUE_NAME[] = L"MouseFinder";
    static constexpr wchar_t ASK_DONE[]   = L"AskDone";
};
