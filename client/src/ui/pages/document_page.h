//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <QDialog>
#include <QDateTime>
#include <QHash>
class QPoint;
class QNetworkAccessManager;
class QPushButton;
class QTableWidget;

// 文档管理页：负责上传文档、刷新文档列表
class DocumentPage : public QDialog {
    Q_OBJECT
public:
    explicit DocumentPage(const QString &serverBaseUrl, QWidget *parent = nullptr);

private:
    struct DocumentRow {
        QString id;
        QString filename;
        QString status;
        int chunkCount{0};
        // 占位项也复用同一套时间字段，渲染时不需要区分“本地行”还是“服务端行”。
        QDateTime createdAt;
    };

    void setupUi();
    // 上传一个文件到服务器
    void uploadFile(const QStringList &filePath);
    // 从服务端拉去文档列表
    void refreshDocuments();
    // 根据当前选中行返回文档 ID。
    QString selectedDocumentId() const;
    // 根据当前选中行返回文件名，供确认提示使用。
    QString selectedDocumentFilename() const;
    // 判断当前选中项是不是本地“上传中”占位记录。
    bool isPendingDocumentSelected() const;
    // 删除当前选中的文档。
    void deleteSelectedDocument();
    // 打开表格右键菜单。
    void showDocumentContextMenu(const QPoint &pos);
    // 将服务端返回的json数据渲染到表格
    void renderDocumentsFromJson(const QByteArray &jsonData);
    // 根据当前服务端文档和本地上传中的占位项统一刷新表格。
    void renderDocumentRows(const QList<DocumentRow> &serverRows);
    // 新增一个本地占位项，让用户在上传尚未完成时也能立刻看到文件。
    QString addPendingUploadRow(const QString &filename);
    // 上传结束后移除本地占位项，避免刷新后出现重复记录。
    void removePendingUploadRow(const QString &pendingId);

private:
    QString serverBaseUrl_;
    QNetworkAccessManager *networkManager_{nullptr};
    QPushButton *uploadButton_{nullptr};
    QPushButton *refreshButton_{nullptr};
    QPushButton *deleteButton_{nullptr};
    QTableWidget *tableWidget_{nullptr};
    // 仅保存在客户端内存中的“上传中”占位项；服务端实际记录返回后会被刷新列表替换。
    QHash<QString, DocumentRow> pendingUploads_;
    // 缓存最近一次从服务端拉到的文档列表，便于和本地占位项一起渲染。
    QList<DocumentRow> serverRowsCache_;
};
