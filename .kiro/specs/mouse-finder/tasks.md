# 实现计划：Mouse Finder

## 概述

按照设计文档将 Mouse Finder 拆分为六个实现阶段：工程搭建 → CursorScaler → MouseHook → CursorAnimator → TrayIcon + StartupManager → AppController 主入口集成。每个阶段产出可独立编译验证的代码增量，最后一步将所有模块串联为单个 `.exe`。

测试策略：单元测试与属性测试均编写为独立的 `test_*.cpp` 文件，由同一 CMake 目标 `mouse_finder_tests` 构建；属性测试使用朴素随机采样（Win32 `rand()`）实现，无需第三方 PBT 框架。

---

## Tasks

- [x] 1. 工程搭建：CMake 构建系统与资源文件
  - 创建 `CMakeLists.txt`，配置 MSVC `/MT` 静态链接、Unicode 字符集、`WIN32` 子系统
  - 链接 `user32.lib gdi32.lib shell32.lib winmm.lib`
  - 创建 `src/resource.h` 和 `src/resource.rc`（托盘图标 `IDI_TRAY`、应用图标）
  - 创建 `src/config.h`，定义 `Config` 命名空间中的所有编译期常量
  - 创建空占位头文件：`src/mouse_hook.h`、`src/cursor_scaler.h`、`src/cursor_animator.h`、`src/tray_icon.h`、`src/startup_manager.h`、`src/app_controller.h`
  - 添加测试目标 `mouse_finder_tests`（`test/test_main.cpp`）并确认可编译为控制台程序
  - _需求：7.4（单文件分发）、7.3（无需提权）_

- [x] 2. 实现 CursorScaler（光标缩放引擎）
  - [x] 2.1 实现 `CursorScaler` 类核心逻辑
    - 在 `src/cursor_scaler.h/.cpp` 中实现 `CaptureCurrentCursors()`：遍历标准光标 ID（OCR_NORMAL、OCR_IBEAM 等约 15 种），用 `CopyImage` 保存原始句柄到 `m_cursors`
    - 实现 `ScaleCursor(HCURSOR src, float factor)`：`GetIconInfo` 获取位图 → `StretchBlt`（HALFTONE）缩放 Color 位图 → `StretchBlt`（BLACKONWHITE）缩放 Mask 位图 → 热点等比缩放 → `CreateIconIndirect` 重建光标，清理临时 GDI 对象
    - 实现 `ApplyScale(float factor)`：对每个 `CursorEntry` 调用 `ScaleCursor`，再调用 `SetSystemCursor`；失败时 `OutputDebugString` 记录并继续
    - 实现 `RestoreCursors()`：调用 `SystemParametersInfo(SPI_SETCURSORS, 0, NULL, 0)` 恢复系统默认光标
    - 处理错误场景：`GetCursor()` 为 NULL 或 `GetIconInfo` 失败时跳过该光标
    - _需求：3.1、3.2、3.3、3.4、3.5、3.6、3.7_

  - [ ]* 2.2 为 `ScaleCursor` 编写属性测试——属性 6（光标缩放尺寸与热点正确性）
    - **属性 6：光标缩放尺寸与热点正确性**
    - 随机生成 `factor ∈ (1.0, 3.0]`，对彩色光标和单色光标分别调用 `ScaleCursor`，用 `GetIconInfo` + `GetObject` 验证输出宽 = `⌊origW × factor⌋`，高 = `⌊origH × factor⌋`，热点坐标 = `⌊hotspot × factor⌋`
    - **验证：需求 3.2、3.3、3.4、8.1、8.2**

  - [ ]* 2.3 为 `RestoreCursors` 编写属性测试——属性 7（缩放-恢复 round-trip）
    - **属性 7：光标缩放-恢复 round-trip**
    - 调用 `ApplyScale(factor)` 后立即调用 `RestoreCursors()`，验证 `GetCursor()` 返回的光标与缩放前系统报告的尺寸/热点一致
    - **验证：需求 3.5**

- [x] 3. 检查点 —— 确保 CursorScaler 编译无错误
  - 确保所有测试通过，如有疑问请向用户确认。

- [x] 4. 实现 MouseHook（鼠标钩子与抖动检测）
  - [x] 4.1 实现 `MouseHook` 类及抖动检测算法
    - 在 `src/mouse_hook.h/.cpp` 中定义类，含静态成员 `s_instance`
    - 实现 `Install(HWND notifyWnd)`：调用 `SetWindowsHookEx(WH_MOUSE_LL, ...)` 并保存 `m_notify`
    - 实现 `Uninstall()`：调用 `UnhookWindowsHookEx`
    - 实现 `SetEnabled(bool enabled)`
    - 实现 `LowLevelMouseProc` 静态回调：过滤非 `WM_MOUSEMOVE` 消息；调用 `CheckShake`；若返回 true 则 `PostMessage(m_notify, WM_SHAKE_DETECTED, 0, 0)` 并调用 `CallNextHookEx`
    - 实现 `CheckShake(POINT pos, DWORD now)`：按设计文档的方向反转计数算法实现，维护 `m_lastX`、`m_lastDir`、`m_reversalCount`、`m_windowStart`
    - _需求：1.1、1.2、1.3、1.4、1.5、1.6、1.7_

  - [ ]* 4.2 为 `CheckShake` 编写属性测试——属性 1（噪声过滤不改变计数）
    - **属性 1：噪声过滤不改变计数**
    - 随机生成一批位移量均 < `MIN_MOVE_PIXELS`（10px）的移动事件序列，验证 `CheckShake` 始终返回 false 且内部计数不增加
    - **验证：需求 1.5**

  - [ ]* 4.3 为 `CheckShake` 编写属性测试——属性 2（有效抖动序列精确触发且归零）
    - **属性 2：有效抖动序列精确触发且归零**
    - 构造在 `SHAKE_WINDOW_MS` 内恰好 4 次有效方向反转的序列，验证第 4 次返回 true 且之后 `m_reversalCount` 为零，后续不会意外再次触发
    - **验证：需求 1.3、1.7**

  - [ ]* 4.4 为 `CheckShake` 编写属性测试——属性 3（时间窗口超时重置）
    - **属性 3：时间窗口超时重置**
    - 随机生成 1~3 次有效反转后，模拟超过 `SHAKE_WINDOW_MS` 的时间间隔，再生成恰好 4 次反转；验证整个过程中不会在窗口超时前触发，重新积累 4 次后才触发
    - **验证：需求 1.4**

  - [ ]* 4.5 为 `CheckShake` 编写属性测试——属性 4（禁用状态下不触发）
    - **属性 4：禁用状态下抖动不触发**
    - `SetEnabled(false)` 后，将任意满足触发条件的序列喂入 `CheckShake`，验证始终返回 false
    - **验证：需求 1.6**

- [x] 5. 检查点 —— 确保 MouseHook 编译及属性测试通过
  - 确保所有测试通过，如有疑问请向用户确认。

- [x] 6. 实现 CursorAnimator（光标动画模块）
  - [x] 6.1 实现 `CursorAnimator` 类及动画状态机
    - 在 `src/cursor_animator.h/.cpp` 中定义类
    - 实现 `Initialize(CursorScaler* scaler)`：保存指针，创建 `CreateTimerQueueTimer` 定时器，回调为静态 `TimerCallback`，间隔 `TIMER_INTERVAL`（16ms）
    - 实现静态 `EaseOut(float t)`：返回 `MAX_SCALE_FACTOR - (1-(1-t)*(1-t)) * (MAX_SCALE_FACTOR - 1.0f)`
    - 实现 `TriggerAnimation()`：设置 `m_state = SHRINKING`，`m_startTime = GetTickCount()`，`m_factor = MAX_SCALE_FACTOR`，立即调用 `m_scaler->ApplyScale(MAX_SCALE_FACTOR)`
    - 实现 `Tick()`：若 `IDLE` 则返回；计算 `t = elapsed / SHRINK_DURATION`；`t >= 1.0` 时调用 `RestoreCursors` 并置 `IDLE`；否则 `m_factor = EaseOut(t)` 并调用 `ApplyScale`
    - 实现 `IsAnimating()`
    - _需求：2.1、2.2、2.3、2.4、2.5、2.6_

  - [ ]* 6.2 为 `EaseOut` 编写属性测试——属性 8（缓动函数值域约束）
    - **属性 8：缓动函数值域约束**
    - 随机采样大量 `t ∈ [0.0, 1.0]`，验证 `EaseOut(t) ∈ [1.0, MAX_SCALE_FACTOR]`
    - **验证：需求 8.3**

  - [ ]* 6.3 为 `EaseOut` 编写属性测试——属性 9（缓动函数单调性）
    - **属性 9：缓动函数单调性**
    - 随机生成 `t1 < t2 ∈ [0.0, 1.0]`，验证 `EaseOut(t1) >= EaseOut(t2)`
    - **验证：需求 8.5**

  - [ ]* 6.4 为 `TriggerAnimation` 编写属性测试——属性 5（动画可重入性）
    - **属性 5：动画可重入性——再触发重置到峰值**
    - 在动画进行中（`SHRINKING` 状态，任意 `factor`）再次调用 `TriggerAnimation()`，验证 `m_factor` 立即等于 `MAX_SCALE_FACTOR`，`m_startTime` 更新为当前时刻
    - **验证：需求 2.5**

- [x] 7. 检查点 —— 确保 CursorAnimator 编译及属性测试通过
  - 确保所有测试通过，如有疑问请向用户确认。

- [x] 8. 实现 TrayIcon（系统托盘模块）
  - [x] 8.1 实现 `TrayIcon` 类
    - 在 `src/tray_icon.h/.cpp` 中定义类
    - 实现 `Create(HWND hostWnd, HINSTANCE hInst)`：填充 `NOTIFYICONDATA`，`uCallbackMessage = WM_USER+1`，加载 `IDI_TRAY` 图标，调用 `Shell_NotifyIcon(NIM_ADD, ...)`
    - 实现 `Destroy()`：调用 `Shell_NotifyIcon(NIM_DELETE, ...)` 并 `DestroyMenu`
    - 实现 `UpdateState(bool enabled)`：更新 `szTip` 为"已启用"或"已暂停"，调用 `Shell_NotifyIcon(NIM_MODIFY, ...)`
    - 在 `hostWnd` 的 `WndProc` 中处理 `WM_USER+1` 消息：`WM_RBUTTONUP` 时 `TrackPopupMenu`，根据当前状态显示「暂停」/「恢复」和「退出」
    - _需求：4.1、4.2、4.3、4.4、4.5、4.6、4.7_

- [x] 9. 实现 StartupManager（开机自启模块）
  - [x] 9.1 实现 `StartupManager` 类
    - 在 `src/startup_manager.h/.cpp` 中定义类
    - 实现 `CheckAndPrompt(HWND parent)`：`RegOpenKeyEx` 读取 `AskDone`；若不存在则调用 `MessageBoxW` 询问用户；根据选择调用 `SetRegistered(true/false)`；写入 `AskDone = 1`
    - 实现 `IsRegistered()`：读取 `HKCU\...\Run\MouseFinder` 是否存在
    - 实现 `SetRegistered(bool enable)`：enable=true 时写入 `Run\MouseFinder = <exe路径>`；enable=false 时删除该值；失败时静默忽略
    - _需求：5.1、5.2、5.3、5.4、5.5、5.6_

- [x] 10. 实现 AppController 与 wWinMain（主入口与集成）
  - [x] 10.1 实现 `AppController` 类
    - 在 `src/app_controller.h/.cpp` 中定义类，持有 `MouseHook`、`CursorAnimator`、`TrayIcon`、`StartupManager` 成员
    - 实现 `Initialize(HINSTANCE hInstance)`：
      1. 注册隐藏窗口类 `MouseFinderHost`，`WndProc` 处理 `WM_SHAKE_DETECTED`、`WM_USER+1`（托盘回调）、`WM_COMMAND`（菜单命令）
      2. `CreateWindowEx` 创建隐藏消息窗口
      3. `m_animator.Initialize(&m_scaler)`（初始化 CursorScaler + CursorAnimator）
      4. `m_scaler.CaptureCurrentCursors()`
      5. `m_hook.Install(hwnd)`；失败则托盘显示"已禁用（钩子安装失败）"
      6. `m_tray.Create(hwnd, hInstance)`
      7. `m_startup.CheckAndPrompt(hwnd)`
    - 实现 `SetEnabled(bool enabled)`：更新 `m_enabled`，调用 `m_hook.SetEnabled`、`m_tray.UpdateState`
    - 实现 `Run()`：标准 `GetMessage / TranslateMessage / DispatchMessage` 消息循环
    - 实现 `Shutdown()`：`m_hook.Uninstall()`、`m_animator` 停止定时器、`m_tray.Destroy()`、`PostQuitMessage(0)`
    - _需求：6.1、6.2、6.3、6.4_

  - [x] 10.2 实现 `wWinMain` 主入口
    - 在 `src/main.cpp` 中实现 `wWinMain`
    - 创建命名互斥量 `MouseFinder_SingleInstance`；若 `ERROR_ALREADY_EXISTS` 则静默退出
    - 构造 `AppController app`，调用 `app.Initialize`；失败时 `MessageBoxW` 提示并返回 1
    - 调用 `app.Run()`（阻塞）
    - `CloseHandle(hMutex)` 后返回 0
    - _需求：6.1、6.2、6.5_

  - [ ]* 10.3 为托盘状态切换编写属性测试——属性 10（托盘启用/禁用状态一致性）
    - **属性 10：托盘启用/禁用状态一致性**
    - 对任意初始状态（enabled/disabled），多次随机调用 `SetEnabled`，验证每次调用后 `IsEnabled()` 返回值、`MouseHook` 内部 `m_enabled` 以及 `TrayIcon` 提示文字三者始终与调用参数一致
    - **验证：需求 4.3、4.4、4.5、4.6**

- [x] 11. 构建验证与最终检查点
  - 使用 `cmake --build` 验证整个项目编译无错误、无警告（`/W4`）
  - 运行 `mouse_finder_tests` 可执行文件，确认所有属性测试和单元测试通过
  - 验证输出产物为单个 `MouseFinder.exe`（无额外 DLL 依赖），用 `dumpbin /dependents` 或 `Dependencies` 工具确认仅依赖 Windows 系统库
  - 确保所有测试通过，如有疑问请向用户确认。

---

## Notes

- 标有 `*` 的子任务为可选项，可跳过以加快 MVP 开发进度
- 每个任务均引用了对应的需求条款以便追溯
- 属性测试使用朴素随机采样实现，不引入第三方 PBT 框架，与"无第三方依赖"约束兼容
- 属性测试中涉及 `SetSystemCursor` 的系统调用，建议在测试时将 `CursorScaler` 的系统调用接口替换为可测试的函数指针或子类，避免污染真实系统光标
- 动画定时器回调运行在系统线程池线程上（`CreateTimerQueueTimer`），`Tick()` 中访问共享状态需注意线程安全；简单方案是在回调中 `PostMessage` 到主线程处理
- 检查点任务是同步点，确保增量可编译、可测试后再继续
