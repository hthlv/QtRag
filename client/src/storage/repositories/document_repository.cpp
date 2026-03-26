//
// Created by hyp on 2026/3/26.
//

#include "document_repository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QVariant>

DocumentRepository::DocumentRepository(const QSqlDatabase &db)
    :db_(db){
}

bool DocumentRepository::insert_or_replace(const DocumentRecord &doc) {
    // 通过 INSERT OR REPLACE 简化主键冲突处理。
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT OR REPLACE INTO documents (id, kb_id, filename, status, size, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(doc.id);
    query.addBindValue(doc.kb_id);
    query.addBindValue(doc.filename);
    query.addBindValue(doc.status);
    query.addBindValue(doc.size);
    query.addBindValue(doc.created_at);
    if (!query.exec()) {
        qWarning() << "insert document failed:" << query.lastError().text();
        return false;
    }
    return true;
}

QVector<DocumentRecord> DocumentRepository::list_all() {
    QSqlQuery query(db_);
    QVector<DocumentRecord> result;

    // 读取全部文档，供文档管理页直接展示。
    if (!query.exec(R"(
        SELECT id, kb_id, filename, status, size, created_at
        FROM documents
        ORDER BY created_at DESC
    )")) {
        qWarning() << "list documents failed:" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        DocumentRecord d;

        // 按 SELECT 字段顺序构造 DocumentRecord。
        d.id = query.value(0).toString();
        d.kb_id = query.value(1).toString();
        d.filename = query.value(2).toString();
        d.status = query.value(3).toString();
        d.size = query.value(4).toLongLong();
        d.created_at = query.value(5).toLongLong();
        result.push_back(d);
    }
    return result;
}
