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

DocumentPage::DocumentPage(const QString &serverBaseUrl, QWidget *parent)
    : QDialog(parent),
      serverBaseUrl_(serverBaseUrl),
      networkManager_(new QNetworkAccessManager(this)) {
    setupUi();
    setWindowTitle("文档管理");
    resize(900, 600);
    // 页面打开后先自动刷新一次列表
    refreshDocuments();
}

void DocumentPage::setupUi() {
    auto *rootLayout = new QVBoxLayout(this);
    // 顶部按钮区
    auto *buttonLayout = new QHBoxLayout();
    uploadButton_ = new QPushButton("上传文档", this);
    refreshButton_ = new QPushButton("刷新列表", this);
    buttonLayout->addWidget(uploadButton_);
    buttonLayout->addWidget(refreshButton_);
    buttonLayout->addStretch();
    // 表格区
    tableWidget_ = new QTableWidget(this);
    tableWidget_->setColumnCount(5);
    tableWidget_->setHorizontalHeaderLabels({
        "文档ID", "文件名", "状态", "Chunk数", "创建时间"
    });
    // 表格列宽自适应
    tableWidget_->horizontalHeader()->setStretchLastSection(true);
    tableWidget_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
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
    const QByteArray fileData = file.readAll();
    file.close();
    QFileInfo fileInfo(filePath);
    // 构造上传请求
    QUrl url(serverBaseUrl_ + "/api/v1/docs/upload");
    QNetworkRequest request(url);
    // 1. 请求体直接放文件内容
    // 2. 用自定义 Header 携带文件名和知识库 id
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    request.setRawHeader("X-Filename", fileInfo.fileName().toUtf8());
    request.setRawHeader("X-Kb-Id", "default");
    QNetworkReply *reply = networkManager_->post(request, fileData);
    // 上传完成后的处理逻辑
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "上传失败", reply->errorString());
            return;
        }
        const QByteArray responseData = reply->readAll();
        // 简单检查服务端返回
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
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
