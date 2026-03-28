//
// Created by hyp on 2026/3/25.
//

#include "main_window.h"
#include "controllers/session_controller.h"
#include "pages/document_page.h"
#include "pages/settings_dialog.h"
#include "pages/reference_panel.h"
#include "notifier.h"
#include "models/session_record.h"
#include "models/message_record.h"
#include "storage/repositories/session_repository.h"
#include "storage/repositories/message_repository.h"
#include "storage/repositories/settings_repository.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QPoint>
#include <QSplitter>
#include <QListWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStatusBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#ifndef QTRAG_CLIENT_HAS_WEBENGINE
#include <QTextCursor>
#include <QTextDocument>
#include <QRegularExpression>
#endif
#include <QSqlDatabase>
#ifdef QTRAG_CLIENT_HAS_WEBENGINE
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#endif
#include <QtGlobal>
#include <algorithm>

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
    if (leftList_->count() > 0) {
        auto *item = leftList_->item(0);
        leftList_->setCurrentItem(item);
        switchToSession(item->data(Qt::UserRole).toString());
    }
}

MainWindow::~MainWindow() {
}


void MainWindow::setupUi() {
    auto *central = new QWidget(this);
    central->setObjectName("MainSurface");
    setCentralWidget(central);
    auto *rootLayout = new QVBoxLayout(central);
    // 主界面整体边距收紧，避免菜单栏下方和三栏区域之间留出过大空白。
    rootLayout->setContentsMargins(12, 6, 12, 10);
    rootLayout->setSpacing(8);
    mainSplitter_ = new QSplitter(this);
    // 分栏拖拽条适当变窄，减少 panel 之间的视觉缝隙。
    mainSplitter_->setHandleWidth(6);

    // 左侧面板用于展示知识库和会话列表。
    auto *leftPanel = new QWidget(this);
    leftPanel->setObjectName("LeftPanel");
    leftPanel->setMinimumWidth(0);
    leftPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(14, 14, 14, 14);
    leftLayout->setSpacing(8);
    auto *sessionTitle = new QLabel("会话", this);
    sessionTitle->setProperty("role", "sectionTitle");
    leftLayout->addWidget(sessionTitle);
    leftList_ = new QListWidget(this);
    leftList_->setObjectName("SessionList");
    // 会话列表支持右键菜单，便于直接删除当前会话。
    leftList_->setContextMenuPolicy(Qt::CustomContextMenu);
    leftLayout->addWidget(leftList_);
    // 点击某个会话时，切换并加载本地历史消息
    connect(leftList_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        QString sessionId = item->data(Qt::UserRole).toString();
        switchToSession(sessionId);
    });
    connect(leftList_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        showSessionContextMenu(pos);
    });

    // 中间区域展示聊天记录，并提供输入框和发送按钮。
    auto *centerPanel = new QWidget(this);
    centerPanel->setObjectName("CenterPanel");
    centerPanel->setMinimumWidth(0);
    centerPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(16, 14, 16, 16);
    centerLayout->setSpacing(10);
    auto *chatTitle = new QLabel("聊天区", this);
    chatTitle->setProperty("role", "sectionTitle");
    centerLayout->addWidget(chatTitle);
#ifdef QTRAG_CLIENT_HAS_WEBENGINE
    chatView_ = new QWebEngineView(this);
    chatView_->setObjectName("ChatView");
    // 聊天区不需要浏览器默认右键菜单，避免暴露无关操作。
    chatView_->setContextMenuPolicy(Qt::NoContextMenu);
    // 允许本地 HTML 页面加载 CDN 的 KaTeX/marked 资源。
    chatView_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    chatViewReady_ = false;
    connect(chatView_, &QWebEngineView::loadFinished, this, [this](bool ok) {
        chatViewReady_ = ok;
        if (ok) {
            renderChatMessages();
        }
    });
    // 先加载固定页面骨架，后续只通过 JS 更新消息数据，避免频繁整页重绘闪烁。
    chatView_->setHtml(buildChatPageHtml(), QUrl("https://qtrag.local/"));
#else
    // 回退模式：系统未安装 Qt WebEngine 时，仍然使用 QTextEdit 渲染气泡。
    chatView_ = new QTextEdit(this);
    chatView_->setObjectName("ChatView");
    chatView_->setReadOnly(true);
    chatView_->setPlaceholderText("这里显示聊天记录...");
    chatView_->document()->setDefaultStyleSheet(QString::fromUtf8(R"(
p { margin: 0 0 8px 0; }
h1, h2, h3, h4 { margin: 4px 0 8px 0; font-weight: 700; }
ul, ol { margin: 4px 0 8px 18px; }
table.md-table {
    border-collapse: collapse;
    margin: 6px 0 10px 0;
    width: 100%;
}
table.md-table th,
table.md-table td {
    border: 1px solid #d9d9d9;
    padding: 6px 8px;
    text-align: left;
}
table.md-table th {
    background: #f6f6f6;
    font-weight: 700;
}
pre {
    background: #1f2329;
    color: #e8edf2;
    border-radius: 8px;
    padding: 8px;
}
code {
    background: #eef2f5;
    border-radius: 4px;
    padding: 1px 4px;
    font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
}
.math-inline {
    background: #eef2f5;
    border-radius: 4px;
    padding: 1px 6px;
    font-family: "Cambria Math", "Times New Roman", serif;
}
.math-block {
    margin: 6px 0 10px 0;
    padding: 8px 10px;
    background: #f5f7f8;
    border-left: 3px solid #07c160;
    border-radius: 6px;
    white-space: pre-wrap;
    font-family: "Cambria Math", "Times New Roman", serif;
}
a { color: #1b7f4f; text-decoration: none; }
)"));
#endif
    centerLayout->addWidget(chatView_, 1);
    auto *inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(10);
    inputEdit_ = new QTextEdit(this);
    inputEdit_->setObjectName("InputEdit");
    inputEdit_->setPlaceholderText("请输入你的问题...");
    inputEdit_->setFixedHeight(100);
    inputEdit_->setAcceptRichText(false);
    inputEdit_->setFocusPolicy(Qt::StrongFocus);
    inputEdit_->setAttribute(Qt::WA_InputMethodEnabled, true);
    inputEdit_->setInputMethodHints(Qt::ImhMultiLine);
    sendButton_ = new QPushButton("发送", this);
    sendButton_->setProperty("variant", "primary");
    sendButton_->setMinimumWidth(92);
    inputLayout->addWidget(inputEdit_, 1);
    inputLayout->addWidget(sendButton_);
    centerLayout->addLayout(inputLayout);

    // 右侧区域预留给检索命中的引用片段。
    auto *rightPanel = new QWidget(this);
    rightPanel->setObjectName("RightPanel");
    rightPanel->setMinimumWidth(0);
    rightPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(14, 14, 14, 14);
    rightLayout->setSpacing(8);
    referenceList_ = new ReferencePanel(this);
    referenceList_->setMinimumWidth(0);
    referenceList_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rightLayout->addWidget(referenceList_);

    // 三栏布局
    mainSplitter_->addWidget(leftPanel);
    mainSplitter_->addWidget(centerPanel);
    mainSplitter_->addWidget(rightPanel);
    mainSplitter_->setStretchFactor(0, 1);
    mainSplitter_->setStretchFactor(1, 5);
    mainSplitter_->setStretchFactor(2, 1);
    mainSplitter_->setChildrenCollapsible(false);
    rootLayout->addWidget(mainSplitter_);
    updateResponsivePanels();
    inputEdit_->setFocus();

    // 点击发送
    connect(sendButton_, &QPushButton::clicked, this, [this]() {
        auto text = inputEdit_->toPlainText().trimmed();
        if (text.isEmpty()) {
            UiNotifier::info(this, "请输入问题");
            return;
        }
        // 如果当前还没有会话，就先创建一个
        if (currentSessionId_.isEmpty()) {
            if (!createNewSession()) {
                return;
            }
        }
        // 先在聊天区显示用户消息
        appendChatMessageToView("user", text);
        // 把用户消息保存到本地数据库
        if (!saveMessageToLocal("user", text)) {
            UiNotifier::warning(this, "提示", "用户消息保存失败");
        }
        inputEdit_->clear();
        // 重置当前 AI 流式缓存
        currentAiMessageBuffer_.clear();
        // 发送网络请求
        // sendChatRequest(text);
        // 发送 SSE 流式请求
        sendChatStreamRequest(text);
    });
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    updateResponsivePanels();
}

void MainWindow::updateResponsivePanels() {
    if (!mainSplitter_) {
        return;
    }

    const int totalWidth = mainSplitter_->width();
    if (totalWidth <= 0) {
        return;
    }

    int leftWidth = std::clamp(totalWidth * 18 / 100, 120, 220);
    int rightWidth = std::clamp(totalWidth * 24 / 100, 140, 320);
    int centerWidth = totalWidth - leftWidth - rightWidth;

    if (centerWidth < 320) {
        int deficit = 320 - centerWidth;
        const int rightShrink = std::min(deficit, std::max(0, rightWidth - 120));
        rightWidth -= rightShrink;
        deficit -= rightShrink;

        const int leftShrink = std::min(deficit, std::max(0, leftWidth - 100));
        leftWidth -= leftShrink;
        deficit -= leftShrink;

        centerWidth = totalWidth - leftWidth - rightWidth;
        if (deficit > 0) {
            centerWidth = std::max(0, centerWidth - deficit);
        }
    }

    mainSplitter_->setSizes({leftWidth, centerWidth, rightWidth});
}

void MainWindow::setupMenu() {
    // 菜单动作先提供占位入口，等业务接通后再替换为真实流程。
    auto *fileMenu = menuBar()->addMenu("文件");
    auto *actionImport = fileMenu->addAction("导入文档");
    auto *actionSettings = fileMenu->addAction("设置");
    auto *sessionMenu = menuBar()->addMenu("会话");
    auto *actionNewSession = sessionMenu->addAction("新建会话");
    auto *actionDeleteSession = sessionMenu->addAction("删除会话");
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
            UiNotifier::info(this, "设置已更新");
        }
    });
    connect(actionNewSession, &QAction::triggered, this, [this]() {
        createNewSession();
    });
    connect(actionDeleteSession, &QAction::triggered, this, [this]() {
        deleteSelectedSession();
    });
}

void MainWindow::showSessionContextMenu(const QPoint &pos) {
    if (!leftList_) {
        return;
    }

    // 右键位置如果命中了某个会话项，就先切换选中，保证菜单动作作用对象明确。
    if (auto *item = leftList_->itemAt(pos)) {
        leftList_->setCurrentItem(item);
    }

    QMenu menu(this);
    auto *deleteAction = menu.addAction("删除会话");
    deleteAction->setEnabled(!selectedSessionId().isEmpty());
    connect(deleteAction, &QAction::triggered, this, [this]() {
        deleteSelectedSession();
    });
    menu.exec(leftList_->viewport()->mapToGlobal(pos));
}

QString MainWindow::selectedSessionId() const {
    if (!leftList_ || !leftList_->currentItem()) {
        return {};
    }
    return leftList_->currentItem()->data(Qt::UserRole).toString();
}

bool MainWindow::deleteSession(const QString &sessionId) {
    if (!sessionController_ || sessionId.trimmed().isEmpty()) {
        return false;
    }

    if (!sessionController_->deleteSession(sessionId)) {
        UiNotifier::warning(this, "错误", "删除会话失败", true);
        return false;
    }

    // 删除完成后重新加载列表，再决定切到哪个会话；如果已经没有会话则清空聊天区。
    loadSessionsFromLocal();
    if (currentSessionId_ == sessionId) {
        currentSessionId_.clear();
        currentAiMessageBuffer_.clear();
        aiMessageStarted_ = false;
        chatMessages_.clear();
        renderChatMessages();
        referenceList_->clearReferences();

        if (leftList_ && leftList_->count() > 0) {
            auto *item = leftList_->item(0);
            leftList_->setCurrentItem(item);
            switchToSession(item->data(Qt::UserRole).toString());
        } else {
            UiNotifier::info(this, "会话已删除");
        }
        return true;
    }

    restoreSessionSelection(currentSessionId_);
    UiNotifier::info(this, "会话已删除");
    return true;
}

void MainWindow::deleteSelectedSession() {
    const QString sessionId = selectedSessionId();
    if (sessionId.isEmpty()) {
        UiNotifier::info(this, "请先选择一个会话");
        return;
    }

    const auto session = sessionRepo_ ? sessionRepo_->findById(sessionId) : std::nullopt;
    const QString sessionTitle = session.has_value() ? session->title : "当前会话";
    const auto confirm = QMessageBox::question(
        this,
        "删除会话",
        QString("确认删除会话“%1”吗？\n本地消息记录也会一并删除。").arg(sessionTitle));
    if (confirm != QMessageBox::Yes) {
        return;
    }

    deleteSession(sessionId);
}

void MainWindow::sendChatRequest(const QString &query) {
    if (!networkManager_) {
        UiNotifier::error(this, "聊天失败", "网络模块未初始化", true);
        return;
    }
    // 构造请求 URL
    QUrl url(serverBaseUrl_ + "/api/v1/chat");
    QNetworkRequest request(url);
    // 请求体直接放 query 文本
    // X-Top-K 指定检索数量
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
    request.setRawHeader("X-Top-K", QByteArray::number(topK_));
    // 发送中禁止按钮，防止重复点击
    sendButton_->setEnabled(false);
    UiNotifier::info(this, "正在请求服务端...");
    QNetworkReply *reply = networkManager_->post(request, query.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        sendButton_->setEnabled(true);
        // 网络错误处理
        if (reply->error() != QNetworkReply::NoError) {
            UiNotifier::warning(this, "聊天失败", reply->errorString(), true);
            return;
        }
        const QByteArray data = reply->readAll();
        // 解析 JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            UiNotifier::warning(this, "聊天失败", parseError.errorString(), true);
            return;
        }
        QJsonObject root = doc.object();
        // 读取 answer
        QString answer = root.value("answer").toString();
        if (answer.isEmpty()) {
            answer = "[空回答]";
        }
        // 显示 AI 回答
        appendChatMessageToView("assistant", answer);
        if (!saveMessageToLocal("assistant", answer)) {
            UiNotifier::warning(this, "提示", "AI 消息保存失败");
        }
        // 渲染右侧引用区
        renderReferences(data);
        UiNotifier::info(this, "回答完成");
    });
}


void MainWindow::renderReferences(const QByteArray &jsonData) {
    referenceList_->clearReferences();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return;
    }
    QJsonObject root = doc.object();
    QJsonArray refs = root.value("references").toArray();
    if (refs.isEmpty()) {
        refs = root.value("refs").toArray();
    }
    referenceList_->setReferences(refs);
}

void MainWindow::sendChatStreamRequest(const QString &query) {
    // 如果上一次请求还没结束，先拒绝重复发送
    if (currentReply_ != nullptr) {
        UiNotifier::info(this, "当前还有请求正在处理");
        return;
    }
    // 清空本轮流式状态
    sseBuffer_.clear();
    aiMessageStarted_ = false;
    referenceList_->clearReferences();
    // referenceList_->addItem("正在等待引用片段");
    QUrl url(serverBaseUrl_ + "/api/v1/chat/stream");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
    request.setRawHeader("X-Top-K", QByteArray::number(topK_));
    sendButton_->setEnabled(false);
    UiNotifier::info(this, "正在流式请求服务端");
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
            UiNotifier::warning(this, "流式请求失败", detail, true);
        } else {
            UiNotifier::info(this, "流式请求结束");
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
        //把本轮 AI 最终回答保存在本地数据库
        if (!currentAiMessageBuffer_.isEmpty()) {
            if (!saveMessageToLocal("assistant", currentAiMessageBuffer_)) {
                UiNotifier::warning(this, "提示", "AI 消息保存失败");
            }
            currentAiMessageBuffer_.clear();
        }
        // 重置标记，便于下一轮流式输出
        aiMessageStarted_ = false;
        UiNotifier::info(this, "回答完成");
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
        UiNotifier::warning(this, "服务端错误", message, true);
        return;
    }
}

void MainWindow::appendAiStreamText(const QString &text) {
    if (text.isEmpty()) {
        return;
    }
    // 第一次收到 token 时创建一个 AI 气泡，后续 token 继续写入同一个气泡。
    if (!aiMessageStarted_ || chatMessages_.isEmpty() || chatMessages_.back().role != "assistant") {
        chatMessages_.push_back({"assistant", QString()});
        aiMessageStarted_ = true;
    }
    chatMessages_.back().content += text;
    // 同时缓存整段 AI 消息，done 时落库
    currentAiMessageBuffer_ += text;
    renderChatMessages();
}

void MainWindow::renderReferencesFromArray(const QJsonArray &refs) {
    referenceList_->clearReferences();
    referenceList_->setReferences(refs);
}

void MainWindow::initializeLocalRepositories() {
    // 从 Qt 全局连接池中取出初始化好的数据库连接
    db_ = QSqlDatabase::database("qtrag_connection");
    sessionRepo_ = std::make_unique<SessionRepository>(db_);
    messageRepo_ = std::make_unique<MessageRepository>(db_);
    settingsRepo_ = std::make_unique<SettingsRepository>(db_);
    sessionController_ = std::make_unique<SessionController>(sessionRepo_.get(), messageRepo_.get());
    // 启动加载设置
    loadSettingsFromLocal();
}

void MainWindow::loadSettingsFromLocal() {
    if (!settingsRepo_) {
        return;
    }

    // 服务端地址为空时回退到本地默认值。
    auto value = settingsRepo_->getValue("server_url");
    if (value.has_value() && !value->trimmed().isEmpty()) {
        serverBaseUrl_ = *value;
    } else {
        serverBaseUrl_ = "http://127.0.0.1:8080";
    }

    // Top-K 统一限制在 1~20，避免把非法值带到请求头。
    auto topkValue = settingsRepo_->getValue("top_k");
    bool ok = false;
    int parsedTopK = topkValue.has_value() ? topkValue->toInt(&ok) : 3;
    if (!ok) {
        parsedTopK = 3;
    }
    topK_ = std::clamp(parsedTopK, 1, 20);
}

void MainWindow::loadSessionsFromLocal() {
    leftList_->clear();
    if (!sessionController_) {
        return;
    }
    auto sessions = sessionController_->loadSessions();
    for (const auto &session: sessions) {
        auto *item = new QListWidgetItem(session.title);
        item->setData(Qt::UserRole, session.id);
        leftList_->addItem(item);
    }
}

bool MainWindow::createNewSession() {
    if (!sessionController_) {
        return false;
    }
    const QString sessionId = sessionController_->createSession();
    if (sessionId.isEmpty()) {
        UiNotifier::warning(this, "错误", "创建会话失败", true);
        return false;
    }
    loadSessionsFromLocal();
    // 切换到刚创建的新会话
    currentSessionId_ = sessionId;
    currentAiMessageBuffer_.clear();
    chatMessages_.clear();
    renderChatMessages();
    referenceList_->clearReferences();
    restoreSessionSelection(currentSessionId_);
    return true;
}

void MainWindow::switchToSession(const QString &sessionId) {
    if (sessionId.isEmpty()) {
        return;
    }
    currentSessionId_ = sessionId;
    currentAiMessageBuffer_.clear();
    aiMessageStarted_ = false;
    referenceList_->clearReferences();
    loadMessageForSession(sessionId);
    UiNotifier::info(this, "已切换会话");
}

void MainWindow::loadMessageForSession(const QString &sessionId) {
    chatMessages_.clear();
    if (!sessionController_) {
        renderChatMessages();
        return;
    }
    auto messages = sessionController_->loadMessages(sessionId);
    for (const auto &msg: messages) {
        chatMessages_.push_back({msg.role, msg.content});
    }
    renderChatMessages();
}

bool MainWindow::saveMessageToLocal(const QString &role, const QString &content) {
    if (currentSessionId_.isEmpty() || !sessionController_) {
        return false;
    }
    bool ok = false;
    if (role == "user") {
        ok = sessionController_->saveUserMessage(currentSessionId_, content);
    } else if (role == "assistant") {
        ok = sessionController_->saveAssistantMessage(currentSessionId_, content);
    }
    if (!ok) {
        return false;
    }
    // 重新加载会话列表，让排序生效
    QString keepSessionId = currentSessionId_;
    loadSessionsFromLocal();
    // 恢复当前选中项
    restoreSessionSelection(keepSessionId);
    return true;
}

void MainWindow::restoreSessionSelection(const QString &sessionId) {
    if (sessionId.isEmpty()) {
        return;
    }
    for (int i = 0; i < leftList_->count(); ++i) {
        auto *item = leftList_->item(i);
        if (item->data(Qt::UserRole).toString() == sessionId) {
            leftList_->setCurrentItem(item);
            break;
        }
    }
}

void MainWindow::appendChatMessageToView(const QString &role, const QString &content) {
    chatMessages_.push_back({role, content});
    renderChatMessages();
}

void MainWindow::renderChatMessages() {
#ifdef QTRAG_CLIENT_HAS_WEBENGINE
    if (!chatView_ || !chatViewReady_) {
        return;
    }

    QJsonArray payload;
    for (const auto &item: chatMessages_) {
        QJsonObject obj;
        obj.insert("role", item.role);
        obj.insert("content", item.content);
        payload.push_back(obj);
    }

    const QString json = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const QString js = QString("window.setChatMessages(%1);").arg(json);
    chatView_->page()->runJavaScript(js);
#else
    if (!chatView_) {
        return;
    }
    QString html;
    html.reserve(chatMessages_.size() * 256);
    for (const auto &item: chatMessages_) {
        html += buildChatBubbleHtml(item.role, item.content);
    }
    chatView_->setHtml(html);
    chatView_->moveCursor(QTextCursor::End);
#endif
}

#ifdef QTRAG_CLIENT_HAS_WEBENGINE
QString MainWindow::buildChatPageHtml() const {
    // WebEngine 页面负责：微信风格气泡布局 + Markdown 解析 + KaTeX 数学公式渲染。
    return QString::fromUtf8(R"QTRAG_CHAT_HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css">
  <script src="https://cdn.jsdelivr.net/npm/marked@12.0.2/marked.min.js"></script>
  <script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js"></script>
  <script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/contrib/auto-render.min.js"></script>
  <style>
    :root {
      --wx-bg: #ededed;
      --wx-user: #95ec69;
      --wx-ai: #ffffff;
      --wx-border: #dfdfdf;
      --wx-text: #191919;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      padding: 8px 10px 10px 10px;
      background: var(--wx-bg);
      color: var(--wx-text);
      font-family: "PingFang SC","Microsoft YaHei UI","Noto Sans CJK SC",sans-serif;
      line-height: 1.5;
    }
    .chat-row {
      width: 100%;
      display: flex;
      align-items: flex-end;
      margin: 4px 0;
      gap: 4px;
      min-width: 0;
    }
    .chat-row.user { justify-content: flex-end; }
    .chat-row.assistant { justify-content: flex-start; }
    .avatar {
      width: 30px;
      height: 30px;
      border-radius: 50%;
      display: inline-flex;
      align-items: center;
      justify-content: center;
      font-size: 11px;
      font-weight: 700;
      user-select: none;
      flex: 0 0 30px;
    }
    .avatar.user { background: #07c160; color: #fff; }
    .avatar.assistant { background: #d8d8d8; color: #4f4f4f; }
    .tail {
      width: 0;
      height: 0;
      border-top: 6px solid transparent;
      border-bottom: 6px solid transparent;
      margin-bottom: 6px;
      flex: 0 0 auto;
    }
    .tail.user { border-left: 7px solid var(--wx-user); }
    .tail.assistant { border-right: 7px solid var(--wx-ai); }
    .bubble {
      display: inline-block;
      width: fit-content;
      max-width: calc(100% - 48px);
      border-radius: 8px;
      border: 1px solid var(--wx-border);
      padding: 8px 10px;
      color: var(--wx-text);
      box-shadow: 0 1px 2px rgba(0,0,0,0.06);
      min-width: 0;
      overflow: hidden;
    }
    .bubble.user { background: var(--wx-user); border-color: #7fd257; }
    .bubble.assistant { background: var(--wx-ai); border-color: var(--wx-border); }
    .sender {
      font-size: 10px;
      color: #6c6c6c;
      margin-bottom: 4px;
    }
    .msg-content p { margin: 0 0 8px 0; }
    .msg-content h1, .msg-content h2, .msg-content h3, .msg-content h4 {
      margin: 4px 0 8px 0;
      font-size: 1em;
      font-weight: 700;
    }
    .msg-content ul, .msg-content ol { margin: 4px 0 8px 18px; }
    .msg-content pre {
      background: #1f2329;
      color: #e8edf2;
      border-radius: 8px;
      padding: 8px;
      overflow: auto;
      margin: 6px 0 10px 0;
    }
    .msg-content code {
      background: #eef2f5;
      border-radius: 4px;
      padding: 1px 4px;
      font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
    }
    .msg-content pre code {
      background: transparent;
      padding: 0;
    }
    .msg-content table {
      border-collapse: collapse;
      margin: 6px 0 10px 0;
      width: 100%;
    }
    .msg-content th, .msg-content td {
      border: 1px solid #d9d9d9;
      padding: 6px 8px;
      text-align: left;
    }
    .msg-content th {
      background: #f6f6f6;
      font-weight: 700;
    }
    .msg-content a {
      color: #1b7f4f;
      text-decoration: none;
    }
    .msg-content,
    .msg-content * {
      min-width: 0;
      overflow-wrap: anywhere;
      word-break: break-word;
    }
  </style>
</head>
<body>
  <div id="chat-root"></div>
  <script>
    window.__pendingMessages = [];

    function escapeHtml(text) {
      return (text || "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#39;");
    }

    function markdownToHtml(text) {
      if (window.marked && typeof window.marked.parse === "function") {
        return window.marked.parse(text || "", { gfm: true, breaks: true });
      }
      return "<p>" + escapeHtml(text || "").replace(/\n/g, "<br/>") + "</p>";
    }

    function renderMath(container) {
      if (window.renderMathInElement) {
        window.renderMathInElement(container, {
          delimiters: [
            { left: "$$", right: "$$", display: true },
            { left: "$", right: "$", display: false },
            { left: "\\(", right: "\\)", display: false },
            { left: "\\[", right: "\\]", display: true }
          ],
          throwOnError: false
        });
      }
    }

    // 统一由 C++ 注入消息 JSON，前端负责按微信布局渲染气泡。
    window.setChatMessages = function(messages) {
      const root = document.getElementById("chat-root");
      if (!root) {
        return;
      }
      window.__pendingMessages = Array.isArray(messages) ? messages : [];
      root.innerHTML = window.__pendingMessages.map((msg) => {
        const isUser = msg.role === "user";
        const sender = isUser ? "我" : "助手";
        const avatar = isUser ? "我" : "AI";
        const rowClass = isUser ? "chat-row user" : "chat-row assistant";
        const bubbleClass = isUser ? "bubble user" : "bubble assistant";
        const leftPart = isUser
          ? ""
          : '<span class="avatar assistant">' + avatar + '</span><span class="tail assistant"></span>';
        const rightPart = isUser
          ? '<span class="tail user"></span><span class="avatar user">' + avatar + '</span>'
          : "";
        return (
          '<div class="' + rowClass + '">' +
            leftPart +
            '<div class="' + bubbleClass + '">' +
              '<div class="sender">' + sender + '</div>' +
              '<div class="msg-content">' + markdownToHtml(msg.content || "") + '</div>' +
            '</div>' +
            rightPart +
          '</div>'
        );
      }).join("");

      renderMath(root);
      window.scrollTo(0, document.body.scrollHeight);
    };

    window.addEventListener("load", function() {
      window.setChatMessages(window.__pendingMessages);
    });
  </script>
</body>
</html>
)QTRAG_CHAT_HTML");
}
#else
QString MainWindow::buildChatBubbleHtml(const QString &role, const QString &content) const {
    const bool isUser = (role == "user");
    const QString sender = isUser ? "我" : "助手";
    QString contentHtml = markdownToHtmlFragment(content);
    if (contentHtml.trimmed().isEmpty()) {
        contentHtml = "<p>&nbsp;</p>";
    }

    const QString bubbleBg = isUser ? "#95ec69" : "#ffffff";
    const QString bubbleFg = "#191919";
    const QString bubbleBorder = isUser ? "#7fd257" : "#e6e6e6";
    const QString senderColor = isUser ? "#3d6a2a" : "#7b7b7b";
    const QString avatarBg = isUser ? "#07c160" : "#d9d9d9";
    const QString avatarFg = isUser ? "#ffffff" : "#4f4f4f";
    const QString avatarText = isUser ? "我" : "AI";
    const QString bubblePart = QString::fromUtf8(R"(
<div style="display:inline-block; width:auto; max-width:calc(100% - 48px); border:1px solid %1; background:%2; color:%3; border-radius:8px; padding:8px 10px;">
  <div style="font-size:10px; margin-bottom:4px; color:%4;">%5</div>
  <div style="font-size:13px; line-height:1.45; overflow-wrap:anywhere; word-break:break-word;">%6</div>
</div>
)")
            .arg(bubbleBorder, bubbleBg, bubbleFg, senderColor, sender, contentHtml);
    const QString avatarPart = QString::fromUtf8(R"(
<span style="display:inline-flex; width:30px; height:30px; border-radius:15px; background:%1; color:%2; align-items:center; justify-content:center; font-size:11px; font-weight:700; vertical-align:bottom;">%3</span>
)")
            .arg(avatarBg, avatarFg, avatarText);
    const QString tailPart = QString::fromUtf8(R"(
<span style="display:inline-block; width:0; height:0; border-top:6px solid transparent; border-bottom:6px solid transparent; %1; vertical-align:bottom; margin:%2;"></span>
)")
            .arg(isUser
                     ? QString("border-left:7px solid %1").arg(bubbleBg)
                     : QString("border-right:7px solid %1").arg(bubbleBg),
                 isUser ? QString("0 6px 6px 3px") : QString("0 3px 6px 6px"));

    if (isUser) {
        return QString::fromUtf8(R"(
<div style="width:100%; margin:4px 0; text-align:right;">
  <span style="display:inline-block; max-width:100%;">%1%2%3</span>
</div>
)")
                .arg(bubblePart, tailPart, avatarPart);
    }
    return QString::fromUtf8(R"(
<div style="width:100%; margin:4px 0; text-align:left;">
  <span style="display:inline-block; max-width:100%;">%1%2%3</span>
</div>
)")
            .arg(avatarPart, tailPart, bubblePart);
}

QString MainWindow::markdownToHtmlFragment(const QString &markdown) const {
    if (markdown.isEmpty()) {
        return QString();
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    // 回退模式沿用 Markdown + 简单数学占位渲染，保证不安装 WebEngine 时仍能看清内容。
    struct PlaceholderPair {
        QString token;
        QString html;
    };
    QVector<PlaceholderPair> placeholders;
    QString markdownWithPlaceholders = markdown;
    int placeholderIndex = 0;

    auto replaceMathBlock = [&]() {
        QRegularExpression blockExpr(QStringLiteral("\\$\\$([\\s\\S]*?)\\$\\$"));
        blockExpr.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
        int searchPos = 0;
        while (true) {
            const QRegularExpressionMatch match = blockExpr.match(markdownWithPlaceholders, searchPos);
            if (!match.hasMatch()) {
                break;
            }
            const QString formulaRaw = match.captured(1).trimmed();
            const QString formulaEscaped = formulaRaw.toHtmlEscaped();
            const QString token = QString("QTRAG_MATH_BLOCK_%1").arg(placeholderIndex++);
            placeholders.push_back({token, QString("<div class=\"math-block\">%1</div>").arg(formulaEscaped)});
            markdownWithPlaceholders.replace(match.capturedStart(0), match.capturedLength(0), token);
            searchPos = match.capturedStart(0) + token.size();
        }
    };

    auto replaceMathInline = [&]() {
        QRegularExpression inlineExpr(QStringLiteral("(?<!\\$)\\$([^\\n$]+?)\\$(?!\\$)"));
        int searchPos = 0;
        while (true) {
            const QRegularExpressionMatch match = inlineExpr.match(markdownWithPlaceholders, searchPos);
            if (!match.hasMatch()) {
                break;
            }
            const QString formulaRaw = match.captured(1).trimmed();
            const QString formulaEscaped = formulaRaw.toHtmlEscaped();
            const QString token = QString("QTRAG_MATH_INLINE_%1").arg(placeholderIndex++);
            placeholders.push_back({token, QString("<span class=\"math-inline\">%1</span>").arg(formulaEscaped)});
            markdownWithPlaceholders.replace(match.capturedStart(0), match.capturedLength(0), token);
            searchPos = match.capturedStart(0) + token.size();
        }
    };

    replaceMathBlock();
    replaceMathInline();

    QTextDocument document;
    document.setMarkdown(markdownWithPlaceholders);
    QString html = document.toHtml();
    const int bodyStartTag = html.indexOf("<body");
    QString fragment = html;
    if (bodyStartTag >= 0) {
        const int bodyStart = html.indexOf('>', bodyStartTag);
        if (bodyStart >= 0) {
            const int bodyEnd = html.indexOf("</body>", bodyStart);
            fragment = (bodyEnd >= 0)
                           ? html.mid(bodyStart + 1, bodyEnd - bodyStart - 1)
                           : html.mid(bodyStart + 1);
        }
    }

    fragment.replace("<table>", "<table class=\"md-table\">");
    fragment.replace("<table ", "<table class=\"md-table\" ");
    for (const auto &placeholder: placeholders) {
        fragment.replace(placeholder.token, placeholder.html);
    }
    return fragment;
#else
    QString escaped = markdown.toHtmlEscaped();
    escaped.replace('\n', "<br/>");
    return QString("<p>%1</p>").arg(escaped);
#endif
}
#endif
