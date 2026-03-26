//
// Created by hyp on 2026/3/25.
//

#pragma once
#include <QMainWindow>
class QListWidget;
class QTextEdit;
class QPushButton;
class QLabel;

// 主窗口负责组织左侧知识库/会话区、中间聊天区和右侧引用区。
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // parent 交给 Qt 父子对象系统管理，默认作为顶层窗口使用。
    explicit MainWindow(QWidget *parent = nullptr);

private:
    // 创建主界面的布局和控件。
    void setupUi();
    // 创建菜单栏和对应的占位动作。
    void setupMenu();

private:
    // 左侧列表同时承载知识库和会话的占位数据。
    QListWidget *leftList_;

    // 中间聊天记录显示区，只读展示消息历史。
    QTextEdit *chatView_;

    // 用户输入问题的编辑框。
    QTextEdit *inputEdit_;

    // 触发发送动作的按钮。
    QPushButton *sendButton_;

    // 右侧引用片段展示区。
    QListWidget *referenceList_;

    // 当前仅保留连接状态字段，后续接入服务端时可以直接复用。
    bool is_connected_{false};
};
