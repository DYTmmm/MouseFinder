cmake_minimum_required(VERSION 3.20)
project(MouseFinder CXX)

# ─── 编译器要求 ────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── MSVC 编译选项 ─────────────────────────────────────────────
if(MSVC)
    # /MT 静态链接 CRT（Release），/MTd（Debug）
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Unicode 字符集
    add_compile_definitions(UNICODE _UNICODE)

    # 警告级别 /W4，将特定警告调低
    add_compile_options(/W4)

    # 禁用 min/max 宏冲突
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ─── 公共头文件目录 ────────────────────────────────────────────
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# ─── 源文件列表（占位头文件无需加入，.cpp 后续任务中添加） ───
set(MOUSEFINDER_SOURCES
    src/main.cpp
    src/app_controller.cpp
    src/cursor_scaler.cpp
    src/mouse_hook.cpp
    src/cursor_animator.cpp
    src/tray_icon.cpp
    src/startup_manager.cpp
    src/resource.rc
)

# ─── 主程序目标：WIN32 子系统 exe ─────────────────────────────
add_executable(MouseFinder WIN32 ${MOUSEFINDER_SOURCES})

target_link_libraries(MouseFinder
    user32.lib
    gdi32.lib
    shell32.lib
    winmm.lib
)

# ─── 测试目标：控制台 exe ─────────────────────────────────────
add_executable(mouse_finder_tests
    test/test_main.cpp
)

target_link_libraries(mouse_finder_tests
    user32.lib
    gdi32.lib
    shell32.lib
    winmm.lib
)

# 测试目标不设 WIN32，默认为控制台子系统
set_target_properties(mouse_finder_tests PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:CONSOLE"
)
