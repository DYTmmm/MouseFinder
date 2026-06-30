#include "startup_manager.h"

// ---------------------------------------------------------------------------
// CheckAndPrompt
// 打开 HKCU\Software\MouseFinder，读取 AskDone 值：
//   - 若不存在（从未询问过）：弹框询问是否开机自启，若用户选 Yes 则注册，
//     然后写入 AskDone=1 防止再次弹框。
//   - 若已存在：直接返回，不弹框。
// 所有注册表操作失败时静默忽略。
// ---------------------------------------------------------------------------
void StartupManager::CheckAndPrompt(HWND parent)
{
    // 尝试打开配置键（不自动创建）
    HKEY hCfg = nullptr;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, REG_CFG, 0, KEY_READ | KEY_WRITE, &hCfg);

    if (res == ERROR_SUCCESS) {
        // 键存在，尝试读取 AskDone 值
        DWORD askDone = 0;
        DWORD dataSize = sizeof(askDone);
        LONG qres = RegQueryValueExW(hCfg, ASK_DONE, nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(&askDone), &dataSize);
        RegCloseKey(hCfg);

        if (qres == ERROR_SUCCESS) {
            // AskDone 已存在，不再询问
            return;
        }
        // AskDone 不存在，继续询问流程
    } else if (res != ERROR_FILE_NOT_FOUND) {
        // 其他错误，静默忽略
        return;
    }
    // res == ERROR_FILE_NOT_FOUND 或键存在但 AskDone 值不存在，均需要询问

    // 弹框询问用户
    int choice = MessageBoxW(parent,
        L"是否设置 Mouse Finder 开机自启动？",
        L"Mouse Finder",
        MB_YESNO | MB_ICONQUESTION);

    if (choice == IDYES) {
        SetRegistered(true);
    }

    // 写入 AskDone = 1，无论用户选择什么，下次不再询问
    HKEY hWrite = nullptr;
    DWORD disposition = 0;
    LONG createRes = RegCreateKeyExW(HKEY_CURRENT_USER, REG_CFG, 0, nullptr,
                                     REG_OPTION_NON_VOLATILE,
                                     KEY_WRITE, nullptr, &hWrite, &disposition);
    if (createRes == ERROR_SUCCESS) {
        DWORD value = 1;
        RegSetValueExW(hWrite, ASK_DONE, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(hWrite);
    }
    // 写入失败时静默忽略
}

// ---------------------------------------------------------------------------
// IsRegistered
// 检测 HKCU\...\Run\MouseFinder 值是否存在。
// ---------------------------------------------------------------------------
bool StartupManager::IsRegistered() const
{
    HKEY hRun = nullptr;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_READ, &hRun);
    if (res != ERROR_SUCCESS) {
        return false;
    }

    LONG qres = RegQueryValueExW(hRun, VALUE_NAME, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(hRun);

    return qres == ERROR_SUCCESS;
}

// ---------------------------------------------------------------------------
// SetRegistered
// enable=true ：获取当前 exe 路径，写入 REG_SZ 到 HKCU\...\Run\MouseFinder
// enable=false：删除 HKCU\...\Run\MouseFinder 值
// 失败时静默忽略。
// ---------------------------------------------------------------------------
void StartupManager::SetRegistered(bool enable)
{
    if (enable) {
        // 获取当前 exe 完整路径
        wchar_t exePath[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            // 获取路径失败，静默忽略
            return;
        }

        HKEY hRun = nullptr;
        LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_WRITE, &hRun);
        if (res != ERROR_SUCCESS) {
            return;
        }

        // 写入 REG_SZ，包含终止符的字节数
        DWORD dataSize = static_cast<DWORD>((len + 1) * sizeof(wchar_t));
        RegSetValueExW(hRun, VALUE_NAME, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(exePath), dataSize);
        RegCloseKey(hRun);
    } else {
        HKEY hRun = nullptr;
        LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN, 0, KEY_WRITE, &hRun);
        if (res != ERROR_SUCCESS) {
            return;
        }

        RegDeleteValueW(hRun, VALUE_NAME);
        RegCloseKey(hRun);
    }
}
