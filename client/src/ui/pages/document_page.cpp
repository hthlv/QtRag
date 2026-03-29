//
// Created by hyp on 2026/3/26.
//

#include "document_page.h"
#include "ui/notifier.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QFileDialog>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTableWidgetItem>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
#else
#include <QTextCodec>
#endif

namespace {
    // 上传前统一把本地文本转换成 UTF-8，避免服务端在 embedding 阶段处理到非法字节。
    bool decode_text_file_to_utf8(const QByteArray &rawData,
                                  QByteArray *utf8Data,
                                  QString *errorMessage) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QStringDecoder utf8(QStringConverter::Utf8);
        const QString utf8Text = utf8.decode(rawData);
        if (!utf8.hasError()) {
            *utf8Data = utf8Text.toUtf8();
            return true;
        }

        // 兼容常见的中文本地文件编码，避免 Windows 下保存的文本直接上传失败。
        QStringDecoder gb18030("GB18030");
        if (gb18030.isValid()) {
            const QString gb18030Text = gb18030.decode(rawData);
            if (!gb18030.hasError()) {
                *utf8Data = gb18030Text.toUtf8();
                return true;
            }
        }
#else
        QTextCodec *utf8Codec = QTextCodec::codecForName("UTF-8");
        QTextCodec *gb18030Codec = QTextCodec::codecForName("GB18030");

        if (utf8Codec) {
            QTextCodec::ConverterState state;
            const QString utf8Text = utf8Codec->toUnicode(rawData.constData(), rawData.size(), &state);
            if (state.invalidChars == 0) {
                *utf8Data = utf8Text.toUtf8();
                return true;
            }
        }

        if (gb18030Codec) {
            QTextCodec::ConverterState state;
            const QString gb18030Text = gb18030Codec->toUnicode(rawData.constData(), rawData.size(), &state);
            if (state.invalidChars == 0) {
                *utf8Data = gb18030Text.toUtf8();
                return true;
            }
        }
#endif

        *errorMessage = "文件编码无法识别，请先转换为 UTF-8 或 GB18030 文本";
        return false;
    }

    // 优先展示服务端返回的业务错误，避免前端只看到笼统的 HTTP 状态描述。
    QString extract_error_message(const QByteArray &responseData, const QString &fallbackMessage) {
        if (responseData.isEmpty()) {
            return fallbackMessage;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            const QString error = doc.object().value("error").toString();
            if (!error.isEmpty()) {
                return error;
            }
        }

        const QString plainText = QString::fromUtf8(responseData).trimmed();
        return plainText.isEmpty() ? fallbackMessage : plainText;
    }
}

DocumentPage::DocumentPage(const QString &serverBaseUrl, QWidget *parent)
    : QDialog(parent),
      serverBaseUrl_(serverBaseUrl),
      networkManager_(new QNetworkAccessManager(this)) {
    setObjectName("DocumentPage");
    setupUi();
    setWindowTitle("文档管理");
    resize(900, 600);
    // 页面打开后先自动刷新一次列表
    refreshDocuments();
}

void DocumentPage::setupUi() {
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(16);
    // 顶部按钮区
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    uploadButton_ = new QPushButton("上传文档", this);
    uploadButton_->setProperty("variant", "primary");
    refreshButton_ = new QPushButton("刷新列表", this);
    refreshButton_->setProperty("variant", "ghost");
    deleteButton_ = new QPushButton("删除文档", this);
    deleteButton_->setProperty("variant", "ghost");
    deleteButton_->setEnabled(false);
    buttonLayout->addWidget(uploadButton_);
    buttonLayout->addWidget(refreshButton_);
    buttonLayout->addWidget(deleteButton_);
    buttonLayout->addStretch();
    // 表格区
    tableWidget_ = new QTableWidget(this);
    tableWidget_->setObjectName("DocumentTable");
    tableWidget_->setColumnCount(5);
    tableWidget_->setHorizontalHeaderLabels({
        "文档ID", "文件名", "状态", "Chunk数", "创建时间"
    });

    // 文档列表列宽：大字段按内容撑开，最后一列继续吃掉剩余空间。
    tableWidget_->horizontalHeader()->setStretchLastSection(true);
    tableWidget_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tableWidget_->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);

    // 文档管理页不需要左侧行号列，直接隐藏让表格更干净。
    tableWidget_->verticalHeader()->setVisible(false);

    tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableWidget_->setAlternatingRowColors(true);
    tableWidget_->setShowGrid(true);
    tableWidget_->setContextMenuPolicy(Qt::CustomContextMenu);
    rootLayout->addLayout(buttonLayout);
    rootLayout->addWidget(tableWidget_);
    // 点击上传按钮：选择文件并上传
    connect(uploadButton_, &QPushButton::clicked, this, [this]() {
        QStringList filePaths = QFileDialog::getOpenFileNames(
            this,
            "选择文档",
            QString(),
            "Text Files (*.txt *.md)"
        );
        if (filePaths.isEmpty()) {
            return;
        }
        uploadFile(filePaths);
    });
    // 点击刷新按钮：拉取服务端文档列表
    connect(refreshButton_, &QPushButton::clicked, this, [this]() {
        refreshDocuments();
    });
    connect(deleteButton_, &QPushButton::clicked, this, [this]() {
        deleteSelectedDocument();
    });
    connect(tableWidget_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        showDocumentContextMenu(pos);
    });
    connect(tableWidget_, &QTableWidget::itemSelectionChanged, this, [this]() {
        deleteButton_->setEnabled(!selectedDocumentId().isEmpty());
    });
}

void DocumentPage::uploadFile(const QStringList &filePaths) {
    for (auto filePath: filePaths) {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            UiNotifier::warning(this, "上传失败", "无法打开文件", true);
            return;
        }
        const QByteArray rawData = file.readAll();
        file.close();
        QByteArray fileData;
        QString decodeError;
        if (!decode_text_file_to_utf8(rawData, &fileData, &decodeError)) {
            UiNotifier::warning(this, "上传失败", decodeError, true);
            return;
        }
        QFileInfo fileInfo(filePath);
        // 构造上传请求
        QUrl url(serverBaseUrl_ + "/api/v1/docs/upload");
        QNetworkRequest request(url);
        // 1. 请求体统一传 UTF-8 文本，和服务端 embedding 的输入要求保持一致
        // 2. 用自定义 Header 携带文件名和知识库 id
        request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
        request.setRawHeader("X-Filename", fileInfo.fileName().toUtf8());
        request.setRawHeader("X-Kb-Id", "default");
        QNetworkReply *reply = networkManager_->post(request, fileData);
        // 上传完成后的处理逻辑
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            const QByteArray responseData = reply->readAll();
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                UiNotifier::warning(
                    this,
                    "上传失败",
                    extract_error_message(responseData, reply->errorString()),
                    true);
                return;
            }
            // 简单检查服务端返回
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
                UiNotifier::warning(this, "上传失败", "服务端返回格式不正确", true);
                return;
            }
            UiNotifier::info(this, "文档已上传");
            refreshDocuments();
        });
    }
}

void DocumentPage::refreshDocuments() {
    UiNotifier::info(this, "正在刷新文档列表...", 2000);
    QUrl url(serverBaseUrl_ + "/api/v1/docs");
    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray responseData = reply->readAll();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            UiNotifier::warning(
                this,
                "刷新失败",
                extract_error_message(responseData, reply->errorString()));
            return;
        }
        renderDocumentsFromJson(responseData);
        UiNotifier::info(this, "文档列表已刷新");
    });
}

QString DocumentPage::selectedDocumentId() const {
    if (!tableWidget_) {
        return {};
    }
    const int row = tableWidget_->currentRow();
    if (row < 0) {
        return {};
    }
    auto *item = tableWidget_->item(row, 0);
    return item ? item->text().trimmed() : QString();
}

QString DocumentPage::selectedDocumentFilename() const {
    if (!tableWidget_) {
        return {};
    }
    const int row = tableWidget_->currentRow();
    if (row < 0) {
        return {};
    }
    auto *item = tableWidget_->item(row, 1);
    return item ? item->text().trimmed() : QString();
}

void DocumentPage::showDocumentContextMenu(const QPoint &pos) {
    if (!tableWidget_) {
        return;
    }

    if (auto *item = tableWidget_->itemAt(pos)) {
        tableWidget_->setCurrentItem(item);
    }

    QMenu menu(this);
    auto *deleteAction = menu.addAction("删除文档");
    deleteAction->setEnabled(!selectedDocumentId().isEmpty());
    connect(deleteAction, &QAction::triggered, this, [this]() {
        deleteSelectedDocument();
    });
    menu.exec(tableWidget_->viewport()->mapToGlobal(pos));
}

void DocumentPage::deleteSelectedDocument() {
    const QString docId = selectedDocumentId();
    if (docId.isEmpty()) {
        UiNotifier::info(this, "请先选择一个文档");
        return;
    }

    const QString filename = selectedDocumentFilename().isEmpty()
                                 ? docId
                                 : selectedDocumentFilename();
    const auto confirm = QMessageBox::question(
        this,
        "删除文档",
        QString("确认删除文档“%1”吗？\n文档文件、分块和向量索引都会一并删除。").arg(filename));
    if (confirm != QMessageBox::Yes) {
        return;
    }

    QUrl url(serverBaseUrl_ + "/api/v1/docs/remove");
    QNetworkRequest request(url);
    request.setRawHeader("X-Doc-Id", docId.toUtf8());
    auto *reply = networkManager_->post(request, QByteArray());
    deleteButton_->setEnabled(false);
    UiNotifier::info(this, "正在删除文档...", 2000);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray responseData = reply->readAll();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            UiNotifier::warning(
                this,
                "删除失败",
                extract_error_message(responseData, reply->errorString()),
                true);
            deleteButton_->setEnabled(!selectedDocumentId().isEmpty());
            return;
        }

        UiNotifier::info(this, "文档已删除");
        refreshDocuments();
    });
}

void DocumentPage::renderDocumentsFromJson(const QByteArray &jsonData) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        UiNotifier::warning(this, "解析失败", "文档列表 JSON 解析失败", true);
        return;
    }
    QJsonObject rootObj = doc.object();
    QJsonArray items = rootObj.value("items").toArray();
    const QString previouslySelectedId = selectedDocumentId();
    tableWidget_->setRowCount(items.count());
    for (int i = 0; i < items.count(); ++i) {
        QJsonObject item = items[i].toObject();
        QString id = item.value("id").toString();
        QString filename = item.value("filename").toString();
        QString status = item.value("status").toString();
        int chunkCount = item.value("chunk_count").toInt();
        qint64 createdAt = static_cast<qint64>(item.value("created_at").toDouble());
        QString createdAtText =
                QDateTime::fromSecsSinceEpoch(createdAt).toString("yyyy-MM-dd HH:mm:ss");

        // 每个单元格都单独设置对齐方式，让表格视觉更整齐。
        auto *idItem = new QTableWidgetItem(id);
        idItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        tableWidget_->setItem(i, 0, idItem);

        auto *filenameItem = new QTableWidgetItem(filename);
        filenameItem->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        tableWidget_->setItem(i, 1, filenameItem);

        auto *statusItem = new QTableWidgetItem(status);
        statusItem->setTextAlignment(Qt::AlignCenter);
        tableWidget_->setItem(i, 2, statusItem);

        auto *chunkCountItem = new QTableWidgetItem(QString::number(chunkCount));
        chunkCountItem->setTextAlignment(Qt::AlignCenter);
        tableWidget_->setItem(i, 3, chunkCountItem);

        auto *createdAtItem = new QTableWidgetItem(createdAtText);
        createdAtItem->setTextAlignment(Qt::AlignCenter);
        tableWidget_->setItem(i, 4, createdAtItem);

        if (!previouslySelectedId.isEmpty() && id == previouslySelectedId) {
            tableWidget_->setCurrentCell(i, 0);
        }
    }

    if (tableWidget_->currentRow() < 0 && tableWidget_->rowCount() > 0) {
        tableWidget_->setCurrentCell(0, 0);
    }
    deleteButton_->setEnabled(!selectedDocumentId().isEmpty());
}
