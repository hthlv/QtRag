//
// Created by hyp on 2026/3/26.
//

#include "document_page.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonParseError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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
    buttonLayout->addWidget(uploadButton_);
    buttonLayout->addWidget(refreshButton_);
    buttonLayout->addStretch();
    // 表格区
    tableWidget_ = new QTableWidget(this);
    tableWidget_->setObjectName("DocumentTable");
    tableWidget_->setColumnCount(5);
    tableWidget_->setHorizontalHeaderLabels({
        "文档ID", "文件名", "状态", "Chunk数", "创建时间"
    });
    // 表格列宽自适应
    tableWidget_->horizontalHeader()->setStretchLastSection(true);
    tableWidget_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableWidget_->setAlternatingRowColors(true);
    rootLayout->addLayout(buttonLayout);
    rootLayout->addWidget(tableWidget_);
    // 点击上传按钮：选择文件并上传
    connect(uploadButton_, &QPushButton::clicked, this, [this]() {
        QString filePath = QFileDialog::getOpenFileName(
            this,
            "选择文档",
            QString(),
            "Text Files (*.txt *.md)"
        );
        if (filePath.isEmpty()) {
            return;
        }
        uploadFile(filePath);
    });
    // 点击刷新按钮：拉取服务端文档列表
    connect(refreshButton_, &QPushButton::clicked, this, [this]() {
        refreshDocuments();
    });
}

void DocumentPage::uploadFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "上传失败", "无法打开文件");
        return;
    }
    const QByteArray rawData = file.readAll();
    file.close();
    QByteArray fileData;
    QString decodeError;
    if (!decode_text_file_to_utf8(rawData, &fileData, &decodeError)) {
        QMessageBox::warning(this, "上传失败", decodeError);
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
            QMessageBox::warning(
                this,
                "上传失败",
                extract_error_message(responseData, reply->errorString()));
            return;
        }
        // 简单检查服务端返回
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(responseData, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QMessageBox::warning(this, "上传失败", "服务端返回格式不正确");
            return;
        }
        QMessageBox::information(this, "上传成功", "文档已上传");
        refreshDocuments();
    });
}

void DocumentPage::refreshDocuments() {
    QUrl url(serverBaseUrl_ + "/api/v1/docs");
    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "刷新失败", reply->errorString());
            return;
        }
        renderDocumentsFromJson(reply->readAll());
    });
}

void DocumentPage::renderDocumentsFromJson(const QByteArray &jsonData) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QMessageBox::warning(this, "解析失败", "文档列表 JSON 解析失败");
        return;
    }
    QJsonObject rootObj = doc.object();
    QJsonArray items = rootObj.value("items").toArray();
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
        tableWidget_->setItem(i, 0, new QTableWidgetItem(id));
        tableWidget_->setItem(i, 1, new QTableWidgetItem(filename));
        tableWidget_->setItem(i, 2, new QTableWidgetItem(status));
        tableWidget_->setItem(i, 3, new QTableWidgetItem(QString::number(chunkCount)));
        tableWidget_->setItem(i, 4, new QTableWidgetItem(createdAtText));
    }
}
