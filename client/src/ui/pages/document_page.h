//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <QDialog>
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
    void setupUi();
    // 上传一个文件到服务器
    void uploadFile(const QStringList &filePath);
    // 从服务端拉去文档列表
    void refreshDocuments();
    // 根据当前选中行返回文档 ID。
    QString selectedDocumentId() const;
    // 根据当前选中行返回文件名，供确认提示使用。
    QString selectedDocumentFilename() const;
    // 删除当前选中的文档。
    void deleteSelectedDocument();
    // 打开表格右键菜单。
    void showDocumentContextMenu(const QPoint &pos);
    // 将服务端返回的json数据渲染到表格
    void renderDocumentsFromJson(const QByteArray &jsonData);
private:
    QString serverBaseUrl_;
    QNetworkAccessManager *networkManager_{nullptr};
    QPushButton *uploadButton_{nullptr};
    QPushButton *refreshButton_{nullptr};
    QPushButton *deleteButton_{nullptr};
    QTableWidget *tableWidget_{nullptr};
};
