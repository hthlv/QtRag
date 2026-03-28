# 客户端 UI 层源码清单：主窗口、样式与统一通知器。
target_sources(${QTRAG_CLIENT_TARGET}
        PRIVATE
        app_style.cpp
        app_style.h
        main_window.cpp
        main_window.h
        notifier.cpp
        notifier.h
)

# UI 子页面模块（文档管理、设置、引用面板）。
add_subdirectory(pages)
