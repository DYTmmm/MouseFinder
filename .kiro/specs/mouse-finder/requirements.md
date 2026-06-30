# 需求文档

## 简介

Mouse Finder 是一款 Windows 后台实用工具，通过检测鼠标快速左右抖动手势，触发系统光标放大动画，帮助用户在多显示器或高分辨率屏幕上快速定位鼠标位置。本文档基于已批准的技术设计文档，将设计意图转化为正式的、可测试的需求。

## 词汇表

- **Mouse_Finder**：本程序整体，包含所有模块
- **MouseHook**：安装 `WH_MOUSE_LL` 全局钩子并执行抖动检测的模块
- **CursorAnimator**：管理光标放大/缩小动画状态机的模块
- **CursorScaler**：执行光标位图捕获、缩放并调用 `SetSystemCursor` 的模块
- **TrayIcon**：系统托盘图标与右键菜单模块
- **StartupManager**：管理开机自启注册表项与询问对话框的模块
- **AppController**：协调所有模块、持有全局状态并运行主消息循环的控制器
- **抖动手势（Shake Gesture）**：在 `SHAKE_WINDOW_MS`（1000ms）时间窗口内，鼠标水平方向反转次数达到 `SHAKE_COUNT`（4）次，且每次移动量不小于 `MIN_MOVE_PIXELS`（10px）的操作序列
- **动画峰值**：`MAX_SCALE_FACTOR = 3.0`，即光标被放大到原始尺寸的 3 倍
- **时间窗口**：长度为 `SHAKE_WINDOW_MS`（1000ms）的滑动检测区间
- **方向反转**：相邻两次有效水平移动（位移 ≥ `MIN_MOVE_PIXELS`）的方向（左/右）发生改变
- **HKCU Run**：注册表路径 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，用于注册开机自启程序

---

## 需求

### 需求 1：抖动手势检测

**用户故事**：作为用户，我希望通过左右快速抖动鼠标来触发光标放大，以便在视觉上快速定位鼠标位置。

#### 验收标准

1. THE MouseHook SHALL 通过安装 `WH_MOUSE_LL` 全局钩子监听所有鼠标移动事件
2. WHEN 相邻两次水平移动方向发生改变且单次移动量 ≥ `MIN_MOVE_PIXELS`（10px），THE MouseHook SHALL 将当前时间窗口内的方向反转计数加一
3. WHEN 时间窗口内方向反转计数达到 `SHAKE_COUNT`（4）次，THE MouseHook SHALL 向 AppController 发送抖动检测信号（`WM_SHAKE_DETECTED`）并将反转计数归零
4. WHEN 自上次记录有效移动起已超过 `SHAKE_WINDOW_MS`（1000ms），THE MouseHook SHALL 将方向反转计数归零并重置时间窗口起始时间
5. IF 单次鼠标水平移动量 < `MIN_MOVE_PIXELS`（10px），THEN THE MouseHook SHALL 忽略该移动事件，不更新方向和计数
6. WHILE MouseHook 处于禁用状态，THE MouseHook SHALL 不执行抖动检测，不发送抖动检测信号
7. WHEN 抖动检测信号被发出后，THE MouseHook SHALL 阻止在当前同一手势序列中再次触发，直到计数归零并重新累计

---

### 需求 2：光标放大动画

**用户故事**：作为用户，我希望在触发抖动手势后光标立即放大到 3 倍并平滑缩回，以便通过视觉变化快速找到光标位置。

#### 验收标准

1. WHEN AppController 收到抖动检测信号，THE CursorAnimator SHALL 立即将系统光标缩放因子设置为 `MAX_SCALE_FACTOR`（3.0）
2. WHILE 动画处于缩小阶段，THE CursorAnimator SHALL 以不超过 `TIMER_INTERVAL`（16ms）的间隔更新光标缩放因子，从 `MAX_SCALE_FACTOR` 平滑缩小至 1.0
3. THE CursorAnimator SHALL 使用缓出二次方曲线（ease-out quadratic）计算每帧缩放因子，总缩小时长为 `SHRINK_DURATION`（1000ms）
4. WHEN 缩小动画完成（elapsed ≥ `SHRINK_DURATION`），THE CursorAnimator SHALL 调用 `CursorScaler.RestoreCursors()` 恢复原始系统光标并将状态置为 IDLE
5. WHEN 动画正在进行中再次收到抖动检测信号，THE CursorAnimator SHALL 将动画重置到峰值（factor = 3.0）并重新开始缩小阶段
6. THE CursorAnimator SHALL 使用高精度定时器（`CreateTimerQueueTimer` 或等效方案）驱动动画 Tick，定时器间隔为 `TIMER_INTERVAL`（16ms）

---

### 需求 3：光标缩放引擎

**用户故事**：作为用户，我希望放大后的光标保持与原始光标相同的形状和颜色，以便准确识别当前光标类型。

#### 验收标准

1. WHEN Mouse_Finder 启动时，THE CursorScaler SHALL 捕获并保存当前所有标准系统光标的原始句柄副本（`CopyImage` 副本）
2. WHEN `ApplyScale(factor)` 被调用，THE CursorScaler SHALL 将每个标准光标位图缩放到 `原始尺寸 × factor`，并调用 `SetSystemCursor` 替换对应系统光标
3. THE CursorScaler SHALL 在缩放时将光标热点坐标等比缩放（`hotspot × factor`），确保缩放后的光标点击位置与视觉位置一致
4. THE CursorScaler SHALL 同时缩放彩色光标的 Color 位图和 AND Mask 位图，保持光标形状与透明区域正确
5. WHEN `RestoreCursors()` 被调用，THE CursorScaler SHALL 通过 `SystemParametersInfo(SPI_SETCURSORS)` 或等效方式将所有系统光标恢复为原始状态
6. IF `SetSystemCursor` 调用失败（返回 FALSE），THEN THE CursorScaler SHALL 静默忽略本帧错误，不中止动画，并向调试输出写入错误信息（`OutputDebugString`）
7. IF `GetCursor()` 返回空句柄或 `GetIconInfo` 失败，THEN THE CursorScaler SHALL 跳过该光标类型的缩放，保持其原始光标不变

---

### 需求 4：系统托盘图标

**用户故事**：作为用户，我希望程序常驻系统托盘并提供右键菜单，以便随时暂停、恢复或退出程序。

#### 验收标准

1. WHEN Mouse_Finder 启动成功后，THE TrayIcon SHALL 在系统托盘区域显示程序图标
2. WHEN 用户右键单击托盘图标，THE TrayIcon SHALL 显示包含「暂停」（当前已启用时）或「恢复」（当前已暂停时）以及「退出」选项的上下文菜单
3. WHEN 用户在菜单中点击「暂停」，THE TrayIcon SHALL 通知 AppController 将程序置为禁用状态，并将菜单项文字更新为「恢复」
4. WHEN 用户在菜单中点击「恢复」，THE TrayIcon SHALL 通知 AppController 将程序置为启用状态，并将菜单项文字更新为「暂停」
5. WHILE 程序处于禁用状态，THE TrayIcon SHALL 更新托盘提示文字以反映「已暂停」状态
6. WHILE 程序处于启用状态，THE TrayIcon SHALL 更新托盘提示文字以反映「已启用」状态
7. WHEN 用户在菜单中点击「退出」，THE TrayIcon SHALL 触发程序正常退出流程，释放所有资源并移除托盘图标

---

### 需求 5：开机自启管理

**用户故事**：作为用户，我希望程序在首次运行时询问是否开机自启，以便在重启后无需手动启动程序。

#### 验收标准

1. WHEN Mouse_Finder 首次启动且从未询问过开机自启偏好（注册表中 `AskDone` 不存在），THE StartupManager SHALL 弹出对话框询问用户是否开机自启
2. WHEN 用户在对话框中选择「是」，THE StartupManager SHALL 向 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 写入 `MouseFinder = <exe完整路径>` 并记录 `AskDone = 1`
3. WHEN 用户在对话框中选择「否」，THE StartupManager SHALL 仅记录 `AskDone = 1`，不写入开机自启注册表项
4. WHEN Mouse_Finder 再次启动且 `AskDone = 1` 已存在，THE StartupManager SHALL 不再弹出询问对话框
5. IF 注册表读写操作失败，THEN THE StartupManager SHALL 静默忽略错误，程序主功能不受影响
6. THE StartupManager SHALL 仅操作 `HKCU`（当前用户）注册表项，不修改 `HKLM` 或需要管理员权限的注册表路径

---

### 需求 6：单实例与程序生命周期

**用户故事**：作为用户，我希望程序同一时间只运行一个实例，以避免重复安装钩子导致的冲突或异常行为。

#### 验收标准

1. WHEN Mouse_Finder 启动时，THE AppController SHALL 创建命名互斥量（`MouseFinder_SingleInstance`）以检测是否已有实例运行
2. IF 命名互斥量已存在（程序已在运行），THEN THE AppController SHALL 静默退出，不显示任何窗口或错误提示
3. IF `WH_MOUSE_LL` 钩子安装失败，THEN THE AppController SHALL 显示错误提示对话框，程序继续运行但托盘图标显示「已禁用（钩子安装失败）」状态
4. WHEN 程序正常退出时，THE AppController SHALL 卸载鼠标钩子、销毁托盘图标、释放定时器资源并关闭互斥量句柄
5. WHEN Mouse_Finder 启动失败（`Initialize` 返回 false），THE AppController SHALL 显示「初始化失败」错误对话框并以非零返回码退出

---

### 需求 7：性能与资源约束

**用户故事**：作为用户，我希望程序在后台运行时对系统性能的影响极小，以便在长时间使用中不影响其他应用程序。

#### 验收标准

1. THE Mouse_Finder SHALL 在正常运行时（非动画状态）内存工作集不超过 15MB
2. THE MouseHook 的低级鼠标钩子回调（`LowLevelMouseProc`）SHALL 在 1ms 内完成执行，避免系统输入延迟
3. THE Mouse_Finder SHALL 以普通用户权限运行，不要求管理员权限
4. THE Mouse_Finder SHALL 以单个 `.exe` 文件分发，不依赖任何第三方运行时库或安装程序
5. THE Mouse_Finder SHALL 兼容 Windows 10 和 Windows 11 操作系统

---

### 需求 8：光标缩放正确性

**用户故事**：作为开发者，我希望光标缩放逻辑在各种输入下均能产生正确结果，以便程序在不同光标类型和缩放因子下均能正常工作。

#### 验收标准

1. THE CursorScaler SHALL 对彩色（Color + Mask）光标和单色（仅 Mask）光标均执行正确缩放
2. FOR ALL 有效的缩放因子 `factor ∈ (1.0, MAX_SCALE_FACTOR]`，THE CursorScaler SHALL 生成尺寸为 `⌊原始宽 × factor⌋ × ⌊原始高 × factor⌋` 的缩放光标
3. FOR ALL 有效的动画进度 `t ∈ [0.0, 1.0]`，THE CursorAnimator SHALL 使缓动函数 `EaseOut(t)` 返回值位于 `[1.0, MAX_SCALE_FACTOR]` 区间内
4. THE CursorAnimator SHALL 使 `EaseOut(0.0)` 返回 `MAX_SCALE_FACTOR`（3.0），`EaseOut(1.0)` 返回 `1.0`
5. THE CursorAnimator SHALL 保证 `EaseOut(t)` 在 `[0.0, 1.0]` 上单调不增（随动画进度光标持续缩小，不出现反弹）
