//
// Created by hyp on 2026/3/25.
//

#include "main_window.h"
#include "pages/document_page.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QSplitter>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStatusBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      networkManager_(new QNetworkAccessManager(this)) {
    // 构造阶段只做界面搭建和基础窗口设置，不放业务逻辑。
    setupUi();
    setupMenu();
    setWindowTitle("QtRAG Client");
    resize(1200, 800);
    statusBar()->showMessage("就绪");
}

void MainWindow::setupUi() {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *rootLayout = new QVBoxLayout(central);
    auto *splitter = new QSplitter(this);

    // 左侧面板用于展示知识库和会话列表。
    auto *leftPanel = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->addWidget(new QLabel("知识库 / 会话", this));
    leftList_ = new QListWidget(this);
    leftList_->addItem("默认知识库");
    leftList_->addItem("会话 1");
    leftList_->addItem("会话 2");
    leftLayout->addWidget(leftList_);

    // 中间区域展示聊天记录，并提供输入框和发送按钮。
    auto *centerPanel = new QWidget(this);
    auto *centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->addWidget(new QLabel("聊天区", this));
    chatView_ = new QTextEdit(this);
    chatView_->setReadOnly(true);
    chatView_->setPlaceholderText("这里显示聊天记录...");
    centerLayout->addWidget(chatView_, 1);
    auto *inputLayout = new QHBoxLayout();
    inputEdit_ = new QTextEdit(this);
    inputEdit_->setPlaceholderText("请输入你的问题...");
    inputEdit_->setFixedHeight(100);
    sendButton_ = new QPushButton("发送", this);
    inputLayout->addWidget(inputEdit_, 1);
    inputLayout->addWidget(sendButton_);
    centerLayout->addLayout(inputLayout);

    // 右侧区域预留给检索命中的引用片段。
    auto *rightPanel = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->addWidget(new QLabel("引用片段", this));
    referenceList_ = new QListWidget(this);
    referenceList_->addItem("引用片段将显示在这里");
    rightLayout->addWidget(referenceList_);

    // 三栏布局
    splitter->addWidget(leftPanel);
    splitter->addWidget(centerPanel);
    splitter->addWidget(rightPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 1);
    rootLayout->addWidget(splitter);

    // 这里先做最小可交互闭环：用户发送后把文本追加到聊天窗口。
    connect(sendButton_, &QPushButton::clicked, this, [this]() {
        auto text = inputEdit_->toPlainText().trimmed();
        if (text.isEmpty()) {
            QMessageBox::information(this, "提示", "请输入问题");
            return;
        }
        // 先在聊天区显示用户消息
        chatView_->append("User: " + text);
        inputEdit_->clear();
        // 发送网络请求
        sendChatRequest(text);
    });
}

void MainWindow::setupMenu() {
    // 菜单动作先提供占位入口，等业务接通后再替换为真实流程。
    auto *fileMenu = menuBar()->addMenu("文件");
    auto *actionImport = fileMenu->addAction("导入文档");
    auto *actionSettings = fileMenu->addAction("设置");
    auto *sessionMenu = menuBar()->addMenu("会话");
    auto *actionNewSession = sessionMenu->addAction("新建会话");
    // 打开文档管理页
    connect(actionImport, &QAction::triggered, this, [this]() {
        DocumentPage page(serverBaseUrl_, this);
        page.exec();
    });
    connect(actionSettings, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "设置", "Day 1 占位：后续实现设置页");
    });
    connect(actionNewSession, &QAction::triggered, this, [this]() {
        leftList_->addItem("新会话");
    });
}

void MainWindow::sendChatRequest(const QString &query) {
    if (!networkManager_) {
        QMessageBox::critical(this, "聊天失败", "网络模块未初始化");
        return;
    }
    // 构造请求 URL
    QUrl url(serverBaseUrl_ + "/api/v1/chat");
    QNetworkRequest request(url);
    // 请求体直接放 query 文本
    // X-Top-K 指定检索数量
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
    request.setRawHeader("X-Top-K", "3");
    // 发送中禁止按钮，防止重复点击
    sendButton_->setEnabled(false);
    statusBar()->showMessage("正在请求服务端...");
    QNetworkReply *reply = networkManager_->post(request, query.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        sendButton_->setEnabled(true);
        // 网络错误处理
        if (reply->error() != QNetworkReply::NoError) {
            statusBar()->showMessage("请求失败");
            QMessageBox::warning(this, "聊天失败", reply->errorString());
            return;
        }
        const QByteArray data = reply->readAll();
        // 解析 JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            statusBar()->showMessage("响应解析失败");
            QMessageBox::warning(this, "聊天失败", parseError.errorString());
            return;
        }
        QJsonObject root = doc.object();
        // 读取 answer
        QString answer = root.value("answer").toString();
        if (answer.isEmpty()) {
            answer = "[空回答]";
        }
        // 显示 AI 回答
        chatView_->append("AI: " + answer);
        chatView_->append("");
        // 渲染右侧引用区
        renderReferences(data);
        statusBar()->showMessage("回答完成");
    });
}


void MainWindow::renderReferences(const QByteArray &jsonData) {
    referenceList_->clear();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        referenceList_->addItem("引用解析失败");
        return;
    }
    QJsonObject root = doc.object();
    QJsonArray refs = root.value("refs").toArray();
    if (refs.isEmpty()) {
        referenceList_->addItem("无引用片段");
        return;
    }
    for (const auto &ref: refs) {
        QJsonObject obj = ref.toObject();
        QString filename = obj.value("filename").toString();
        double score = obj.value("score").toDouble();
        QString text = obj.value("text").toString();
        QString itemText = QString("[%1] score=%2\n%3")
                .arg(filename.isEmpty() ? "unknown" : filename)
                .arg(score, 0, 'f', 3)
                .arg(text.left(120));
        referenceList_->addItem(itemText);
    }
}
