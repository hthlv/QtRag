//
// Created by hyp on 2026/3/26.
//

#include "message_repository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

MessageRepository::MessageRepository(const QSqlDatabase &db)
    :db_(db){
}

bool MessageRepository::insert(const MessageRecord &message) {
    // 预编译语句写入消息记录。
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT INTO messages (id, session_id, role, content, status, created_at)
        VALUES (?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(message.id);
    query.addBindValue(message.session_id);
    query.addBindValue(message.role);
    query.addBindValue(message.content);
    query.addBindValue(message.status);
    query.addBindValue(message.created_at);
    if (!query.exec()) {
        qWarning() << "insert message failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool MessageRepository::removeBySessionId(const QString &sessionId) {
    // 会话删除前需要先把关联消息删掉，避免本地数据库残留孤儿记录。
    QSqlQuery query(db_);
    query.prepare(R"(
        DELETE FROM messages
        WHERE session_id = ?
    )");
    query.addBindValue(sessionId);
    if (!query.exec()) {
        qWarning() << "remove messages by session id failed:" << query.lastError().text();
        return false;
    }
    return true;
}

QVector<MessageRecord> MessageRepository::listBySessionId(const QString &sessionId) {
    QSqlQuery query(db_);
    QVector<MessageRecord> result;

    // 读取指定会话的全部消息，保持时间顺序。
    query.prepare(R"(
        SELECT id, session_id, role, content, status, created_at
        FROM messages
        WHERE session_id = ?
        ORDER BY created_at ASC
    )");
    query.addBindValue(sessionId);
    if (!query.exec()) {
        qWarning() << "list messages failed:" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        MessageRecord m;

        // 列下标与 SELECT 字段顺序一一对应。
        m.id = query.value(0).toString();
        m.session_id = query.value(1).toString();
        m.role = query.value(2).toString();
        m.content = query.value(3).toString();
        m.status = query.value(4).toString();
        m.created_at = query.value(5).toLongLong();
        result.push_back(m);
    }
    return result;
}
