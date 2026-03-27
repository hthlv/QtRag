//
// Created by hyp on 2026/3/25.
//

#include "main_window.h"
#include "pages/document_page.h"
#include "pages/settings_dialog.h"
#include "models/session_record.h"
#include "models/message_record.h"
#include "storage/repositories/session_repository.h"
#include "storage/repositories/message_repository.h"
#include "storage/repositories/settings_repository.h"
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
#include <QTextCursor>
#include <QDateTime>
#include <QSqlDatabase>
#include <QUuid>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      networkManager_(new QNetworkAccessManager(this)) {
    // 先初始化本地 repository
    initializeLocalRepositories();
    // 构造阶段只做界面搭建和基础窗口设置，不放业务逻辑。
    setupUi();
    setupMenu();
    setWindowTitle("QtRAG Client");
    resize(1200, 800);
    statusBar()->showMessage("就绪");
    // 启动时加载本地会话
    loadSessionsFromLocal();
    // 如果本地没有会话，就自动创建一个
    if (leftList_->count() == 0) {
        createNewSession();
    } else {
        auto *item = leftList_->item(0);
        leftList_->setCurrentItem(item);
        switchToSession(item->data(Qt::UserRole).toString());
    }
}

MainWindow::~MainWindow() {
}


void MainWindow::setupUi() {
    auto *central = new QWidget(this);
    setCentralWidget(central);
    auto *rootLayout = new QVBoxLayout(central);
    auto *splitter = new QSplitter(this);

    // 左侧面板用于展示知识库和会话列表。
    auto *leftPanel = new QWidget(this);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->addWidget(new QLabel("会话", this));
    leftList_ = new QListWidget(this);
    leftLayout->addWidget(leftList_);
    // 点击某个会话时，切换并加载本地历史消息
    connect(leftList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        QString sessionId = item->data(Qt::UserRole).toString();
        switchToSession(sessionId);
    });

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
    inputEdit_->setAcceptRichText(false);
    inputEdit_->setFocusPolicy(Qt::StrongFocus);
    inputEdit_->setAttribute(Qt::WA_InputMethodEnabled, true);
    inputEdit_->setInputMethodHints(Qt::ImhMultiLine);
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
    inputEdit_->setFocus();

    // 点击发送
    connect(sendButton_, &QPushButton::clicked, this, [this]() {
        auto text = inputEdit_->toPlainText().trimmed();
        if (text.isEmpty()) {
            QMessageBox::information(this, "提示", "请输入问题");
            return;
        }
        // 如果当前还没有会话，就先创建一个
        if (currentSessionId_.isEmpty()) {
            createNewSession();
        }
        // 先在聊天区显示用户消息
        chatView_->append("User: " + text);
        chatView_->append("");
        // 把用户消息保存到本地数据库
        saveMessageToLocal("user", text, "done");
        inputEdit_->clear();
        // 重置当前 AI 流式缓存
        currentAiMessageBuffer_.clear();
        // 发送网络请求
        // sendChatRequest(text);
        // 发送 SSE 流式请求
        sendChatStreamRequest(text);
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
        SettingsDialog dialog(settingsRepo_.get(), this);
        if (dialog.exec() == QDialog::Accepted) {
            // 保存成功后重新加载配置
            loadSettingsFromLocal();
            statusBar()->showMessage("设置已更新");
        }
    });
    connect(actionNewSession, &QAction::triggered, this, [this]() {
        createNewSession();
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

void MainWindow::sendChatStreamRequest(const QString &query) {
    // 如果上一次请求还没结束，先拒绝重复发送
    if (currentReply_ != nullptr) {
        QMessageBox::information(this, "提示", "当前还有请求正在处理");
        return;
    }
    // 清空本轮流式状态
    sseBuffer_.clear();
    aiMessageStarted_ = false;
    referenceList_->clear();
    referenceList_->addItem("正在等待引用片段");
    QUrl url(serverBaseUrl_ + "/api/v1/chat/stream");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
    request.setRawHeader("X-Top-K", "3");
    sendButton_->setEnabled(false);
    statusBar()->showMessage("正在流式请求服务端");
    currentReply_ = networkManager_->post(request, query.toUtf8());
    // readyRead: 每来一条数据就读出来
    connect(currentReply_, &QNetworkReply::readyRead, this, [this]() {
        if (!currentReply_) {
            return;
        }
        // 只读取当前已到达的数据，未到达的内容继续留给后续 readyRead 处理。
        QByteArray chunk = currentReply_->readAll();
        // 为了更好解析 SSE，这里把 CRLF 统一转成 LF
        chunk.replace("\r\n", "\n");
        // SSE 事件可能跨多个网络包，因此先进入缓冲区，再按事件边界解析。
        sseBuffer_.append(chunk);
        processSseBuffer();
    });
    // finished: 请求结束时做收尾
    connect(currentReply_, &QNetworkReply::finished, this, [this]() {
        if (!currentReply_) {
            return;
        }
        // 再读一次，防止还有尾部数据没处理
        QByteArray tail = currentReply_->readAll();
        tail.replace("\r\n", "\n");
        if (!tail.isEmpty()) {
            sseBuffer_.append(tail);
            processSseBuffer();
        }
        // 如果网络层面报错，需要提示
        if (currentReply_->error() != QNetworkReply::NoError) {
            QByteArray errorBody = currentReply_->readAll();
            QString detail = currentReply_->errorString();
            if (!errorBody.isEmpty()) {
                detail += "\n\n服务端返回：\n" + QString::fromUtf8(errorBody);
            }
            QMessageBox::warning(this, "流式请求失败", detail);
            statusBar()->showMessage("流式请求失败");
        } else {
            statusBar()->showMessage("流式请求结束");
        }
        currentReply_->deleteLater();
        currentReply_ = nullptr;
        sendButton_->setEnabled(true);
    });
}

void MainWindow::processSseBuffer() {
    // SSE 事件之间用空行分隔，即 "\n\n"
    while (true) {
        int pos = sseBuffer_.indexOf("\n\n");
        if (pos < 0) {
            break;
        }
        QByteArray block = sseBuffer_.left(pos);
        sseBuffer_.remove(0, pos + 2);
        if (!block.trimmed().isEmpty()) {
            processSseEventBlock(QString::fromUtf8(block));
        }
    }
}

void MainWindow::processSseEventBlock(const QString &block) {
    QString eventName;
    QString dataLine;
    const QStringList lines = block.split('\n', Qt::SkipEmptyParts);
    // 解析 event 和 data
    for (const QString &line: lines) {
        if (line.startsWith("event:")) {
            eventName = line.mid(QString("event:").size()).trimmed();
        } else if (line.startsWith("data:")) {
            dataLine = line.mid(QString("data:").size()).trimmed();
        }
    }
    if (eventName.isEmpty()) {
        return;
    }
    // token 事件：追加回答文本
    if (eventName == "token") {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(dataLine.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            return;
        }
        QString content = doc.object().value("content").toString();
        appendAiStreamText(content);
        return;
    }
    if (eventName == "refs") {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(dataLine.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            return;
        }
        QJsonArray refs = doc.object().value("references").toArray();
        renderReferencesFromArray(refs);
        return;
    }
    // done 事件：本轮结束
    if (eventName == "done") {
        // 给 AI 回答补两个换行，便于下一轮显示
        if (aiMessageStarted_) {
            chatView_->moveCursor(QTextCursor::End);
            chatView_->insertPlainText("\n\n");
        }
        //把本轮 AI 最终回答保存在本地数据库
        if (!currentAiMessageBuffer_.isEmpty()) {
            saveMessageToLocal("assistant", currentAiMessageBuffer_, "done");
            currentAiMessageBuffer_.clear();
        }
        // 重置标记，便于下一轮流式输出
        aiMessageStarted_ = false;
        statusBar()->showMessage("回答完成");
        return;
    }
    // error 事件：服务端逻辑错误
    if (eventName == "error") {
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(dataLine.toUtf8(), &parseError);
        QString message = "未知错误";
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            message = doc.object().value("message").toString(message);
        }
        QMessageBox::warning(this, "服务端错误", message);
        statusBar()->showMessage("服务端返回错误");
        return;
    }
}

void MainWindow::appendAiStreamText(const QString &text) {
    chatView_->moveCursor(QTextCursor::End);
    // 第一次收到 token 时，先输出 AI：
    if (!aiMessageStarted_) {
        chatView_->insertPlainText("AI: ");
        aiMessageStarted_ = true;
    }
    // 逐步追加文本
    chatView_->insertPlainText(text);
    // 同时缓存整段 AI 消息，done 时落库
    currentAiMessageBuffer_ += text;
    // 保证滚动条始终跟到底部
    chatView_->moveCursor(QTextCursor::End);
}

void MainWindow::renderReferencesFromArray(const QJsonArray &refs) {
    referenceList_->clear();
    if (refs.isEmpty()) {
        referenceList_->addItem("无引用片段");
        return;
    }
    for (const auto &value: refs) {
        QJsonObject obj = value.toObject();
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

void MainWindow::initializeLocalRepositories() {
    // 从 Qt 全局连接池中取出初始化好的数据库连接
    db_ = QSqlDatabase::database("qtrag_connection");
    sessionRepo_ = std::make_unique<SessionRepository>(db_);
    messageRepo_ = std::make_unique<MessageRepository>(db_);
    settingsRepo_ = std::make_unique<SettingsRepository>(db_);
    // 启动加载设置
    loadSettingsFromLocal();
}

void MainWindow::loadSettingsFromLocal() {
    if (!settingsRepo_) {
        return;
    }
    auto value = settingsRepo_->getValue("server_url");
    if (value.has_value() && !value->trimmed().isEmpty()) {
        serverBaseUrl_ = *value;
    } else {
        serverBaseUrl_ = "http://127.0.0.1:8080";
    }
}

void MainWindow::loadSessionsFromLocal() {
    leftList_->clear();
    auto sessions = sessionRepo_->listAll();
    for (const auto &session: sessions) {
        auto *item = new QListWidgetItem(session.title);
        item->setData(Qt::UserRole, session.id);
        leftList_->addItem(item);
    }
}

void MainWindow::createNewSession() {
    SessionRecord session;
    session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.title = "新会话 " + QDateTime::currentDateTime().toString("MM-dd HH:mm:ss");
    session.kb_id = "default";
    session.created_at = QDateTime::currentSecsSinceEpoch();
    session.updated_at = QDateTime::currentSecsSinceEpoch();
    if (!sessionRepo_->insert(session)) {
        QMessageBox::warning(this, "错误", "创建会话失败");
        return;
    }
    loadSessionsFromLocal();
    // 切换到刚创建的新会话
    currentSessionId_ = session.id;
    chatView_->clear();
    for (int i = 0; i < leftList_->count(); ++i) {
        auto *item = leftList_->item(i);
        if (item->data(Qt::UserRole).toString() == currentSessionId_) {
            leftList_->setCurrentItem(item);
            break;
        }
    }
}

void MainWindow::switchToSession(const QString &sessionId) {
    if (sessionId.isEmpty()) {
        return;
    }
    currentSessionId_ = sessionId;
    loadMessageForSession(sessionId);
    statusBar()->showMessage("已切换会话");
}

void MainWindow::loadMessageForSession(const QString &sessionId) {
    chatView_->clear();
    auto messages = messageRepo_->listBySessionId(sessionId);
    for (const auto &msg: messages) {
        if (msg.role == "user") {
            chatView_->append("User: " + msg.content);
        } else {
            chatView_->append("AI: " + msg.content);
        }
        chatView_->append("");
    }
}

void MainWindow::saveMessageToLocal(const QString &role,
                                    const QString &content,
                                    const QString &status) {
    if (currentSessionId_.isEmpty()) {
        return;
    }
    MessageRecord message;
    message.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    message.session_id = currentSessionId_;
    message.role = role;
    message.content = content;
    message.status = status;
    message.created_at = QDateTime::currentSecsSinceEpoch();
    messageRepo_->insert(message);
    // 同时更新会话的 updated_at，让最近活跃会话排在前面
    sessionRepo_->touch(currentSessionId_, QDateTime::currentSecsSinceEpoch());
    // 重新加载会话列表，让排序生效
    QString keepSessionId = currentSessionId_;
    loadSessionsFromLocal();
    // 恢复当前选中项
    for (int i = 0; i < leftList_->count(); ++i) {
        auto *item = leftList_->item(i);
        if (item->data(Qt::UserRole).toString() == keepSessionId) {
            leftList_->setCurrentItem(item);
            break;
        }
    }
}
