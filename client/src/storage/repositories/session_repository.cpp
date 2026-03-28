//
// Created by hyp on 2026/3/26.
//

#include "session_repository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

SessionRepository::SessionRepository(const QSqlDatabase &db)
    :db_(db){
}

bool SessionRepository::insert(const SessionRecord &session) {
    // 使用预编译 SQL 防止拼接错误并自动处理参数转义。
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT INTO sessions (id, title, kb_id, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?)
    )");
    query.addBindValue(session.id);
    query.addBindValue(session.title);
    query.addBindValue(session.kb_id);
    query.addBindValue(session.created_at);
    query.addBindValue(session.updated_at);
    if (!query.exec()) {
        qWarning() << "insert session failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool SessionRepository::removeById(const QString &sessionId) {
    QSqlQuery query(db_);
    // 删除单个会话记录；调用方负责先清理关联消息。
    query.prepare(R"(
        DELETE FROM sessions
        WHERE id = ?
    )");
    query.addBindValue(sessionId);
    if (!query.exec()) {
        qWarning() << "remove session failed:" << query.lastError().text();
        return false;
    }
    return true;
}

QVector<SessionRecord> SessionRepository::listAll() {
    QSqlQuery query(db_);
    QVector<SessionRecord> result;

    // 读取全部会话，用于左侧会话列表展示。
    if (!query.exec(R"(
        SELECT id, title, kb_id, created_at, updated_at
        FROM sessions
        ORDER BY updated_at DESC
    )")) {
        qWarning() << "list sessions failed:" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        SessionRecord s;
        s.id = query.value(0).toString();
        s.title = query.value(1).toString();
        s.kb_id = query.value(2).toString();
        s.created_at = query.value(3).toLongLong();
        s.updated_at = query.value(4).toLongLong();
        result.push_back(s);
    }
    return result;
}

std::optional<SessionRecord> SessionRepository::findById(const QString &id) {
    QSqlQuery query(db_);

    // 精确查询一条会话记录。
    query.prepare(R"(
        SELECT id, title, kb_id, created_at, updated_at
        FROM sessions
        WHERE id = ?
        LIMIT 1
    )");
    query.addBindValue(id);
    if (!query.exec()) {
        qWarning() << "find session failed:" << query.lastError().text();
        return std::nullopt;
    }
    if (query.next()) {
        SessionRecord s;

        // 字段顺序与 SELECT 子句保持一致。
        s.id = query.value(0).toString();
        s.title = query.value(1).toString();
        s.kb_id = query.value(2).toString();
        s.created_at = query.value(3).toLongLong();
        s.updated_at = query.value(4).toLongLong();
        return s;
    }
    return std::nullopt;
}

bool SessionRepository::touch(const QString &id, qint64 updatedAt) {
    QSqlQuery query(db_);
    query.prepare(R"(
        UPDATE sessions
        SET updated_at = ?
        WHERE id = ?
    )");
    query.addBindValue(updatedAt);
    query.addBindValue(id);
    if (!query.exec()) {
        qWarning() << "touch session failed:" << query.lastError().text();
        return false;
    }
    return true;
}

bool SessionRepository::updateTitle(const QString &id, const QString &title, qint64 updatedAt) {
    QSqlQuery query(db_);
    query.prepare(R"(
        UPDATE sessions
        SET title = ?, updated_at = ?
        WHERE id = ?
    )");
    query.addBindValue(title);
    query.addBindValue(updatedAt);
    query.addBindValue(id);
    if (!query.exec()) {
        qWarning() << "update session title failed:" << query.lastError().text();
        return false;
    }
    return true;
}
