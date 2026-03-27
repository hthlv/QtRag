//
// Created by hyp on 2026/3/25.
//

#pragma once
#include <QMainWindow>
class QListWidget;
class QTextEdit;
class QPushButton;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;

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

    // 非流式接口
    // 发送聊天请求
    void sendChatRequest(const QString &query);

    // 渲染引用列表
    void renderReferences(const QByteArray &jsonData);

    // 流式接口
    // 发送 SSE 流式聊天请求
    void sendChatStreamRequest(const QString &query);

    // 处理当前缓冲区中的 SSE 数据
    void processSseBuffer();

    // 处理单个 SSE 事件块
    void processSseEventBlock(const QString &block);

    // 追加 AI 流式文本
    void appendAiStreamText(const QString &text);

    // 渲染引用列表
    void renderReferencesFromArray(const QJsonArray &refs);

private:
    // 左侧列表同时承载知识库和会话的占位数据。
    QListWidget *leftList_{nullptr};
    // 中间聊天记录显示区，只读展示消息历史。
    QTextEdit *chatView_{nullptr};
    // 用户输入问题的编辑框。
    QTextEdit *inputEdit_{nullptr};
    // 触发发送动作的按钮。
    QPushButton *sendButton_{nullptr};
    // 右侧引用片段展示区。
    QListWidget *referenceList_{nullptr};
    // 当前仅保留连接状态字段，后续接入服务端时可以直接复用。
    bool is_connected_{false};
    // HTTP 网络管理器
    QNetworkAccessManager *networkManager_{nullptr};
    QNetworkReply* currentReply_{nullptr};
    // 用于缓存尚未解析完成的 SSE 文本
    QByteArray sseBuffer_;
    // 标记当前这轮 AI 回答是否已经开始显示
    bool aiMessageStarted_{false};

    QString serverBaseUrl_{"http://127.0.0.1:8080"};
};
