#include "app_controller.h"

// wWinMain —— 程序主入口
// 前置条件：Windows 10 或更高版本
// 后置条件：所有模块正确初始化，消息循环退出后资源全部释放
int WINAPI wWinMain(
    HINSTANCE hInst,
    HINSTANCE /*hPrevInstance*/,
    PWSTR     /*lpCmdLine*/,
    int       /*nCmdShow*/)
{
    // 防止重复启动：命名互斥量
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"MouseFinder_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        return 0;
    }

    AppController app;
    if (!app.Initialize(hInst))
    {
        MessageBoxW(nullptr, L"初始化失败", L"Mouse Finder", MB_ICONERROR);
        CloseHandle(hMutex);
        return 1;
    }

    app.Run();   // 阻塞，直到用户选择"退出"

    CloseHandle(hMutex);
    return 0;
}
