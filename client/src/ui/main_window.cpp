//
// Created by hyp on 2026/3/25.
//

#include "main_window.h"
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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      leftList_(nullptr),
      chatView_(nullptr),
      inputEdit_(nullptr),
      sendButton_(nullptr),
      referenceList_(nullptr) {
    // 构造阶段只做界面搭建和基础窗口设置，不放业务逻辑。
    setupUi();
    setupMenu();
    setWindowTitle("QtRAG Client");
    resize(1200, 800);
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
        chatView_->append("User: " + text);
        chatView_->append("AI: [占位回复，后续接服务端]");
        inputEdit_->clear();
    });

    statusBar()->showMessage("未连接服务器");
}

void MainWindow::setupMenu() {
    // 菜单动作先提供占位入口，等业务接通后再替换为真实流程。
    auto *fileMenu = menuBar()->addMenu("文件");
    auto *actionImport = fileMenu->addAction("导入文档");
    auto *actionSettings = fileMenu->addAction("设置");
    auto *sessionMenu = menuBar()->addMenu("会话");
    auto *actionNewSession = sessionMenu->addAction("新建会话");
    connect(actionImport, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "导入文档", "Day 1 占位：后续实现上传文档");
    });
    connect(actionSettings, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, "设置", "Day 1 占位：后续实现设置页");
    });
    connect(actionNewSession, &QAction::triggered, this, [this]() {
        leftList_->addItem("新会话");
    });
}
