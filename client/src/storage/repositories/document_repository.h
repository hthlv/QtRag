//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "models/document_record.h"
#include <QSqlDatabase>
#include <QVector>

// 负责 documents 表的读写操作。
class DocumentRepository {
public:
    // db 由外部传入并复用。
    explicit DocumentRepository(const QSqlDatabase& db);

    // 插入或覆盖文档记录（主键冲突时更新整行）。
    bool insertOrReplace(const DocumentRecord &doc);

    // 查询全部文档，按创建时间倒序。
    QVector<DocumentRecord> listAll();

private:
    // 当前仓储使用的数据库连接。
    QSqlDatabase db_;
};
