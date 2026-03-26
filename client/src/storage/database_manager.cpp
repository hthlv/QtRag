//
// Created by hyp on 2026/3/26.
//

#include "database_manager.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>

DatabaseManager::DatabaseManager(const QString &dbPath)
    :dbPath_(dbPath){
}

bool DatabaseManager::open() {
    // 使用固定连接名，保证同一进程内重复打开时能复用已有连接。
    static const QString connectionName = "qtrag_connection";

    if (QSqlDatabase::contains(connectionName)) {
        db_ = QSqlDatabase::database(connectionName);
    } else {
        // 第一个参数是驱动名，第二个参数才是连接名。
        db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db_.setDatabaseName(dbPath_);
    }

    if (!db_.open()) {
        qWarning() << "[DB] open failed" << db_.lastError().text();
        return false;
    }

    // 打开成功后打印实际数据库文件，便于排查路径是否正确。
    qDebug() << "[DB] opened: " << dbPath_;
    return true;
}

void DatabaseManager::close() {
    if (db_.isOpen()) {
        db_.close();
        qDebug() << "[DB] Closed";
    }
}

bool DatabaseManager::initializeSchema() {
    // 会话表保存每个聊天会话的元信息。
    const QString createSessions = R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            kb_id TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        )
    )";

    // 消息表保存会话中的对话消息。
    const QString createMessages = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            status TEXT NOT NULL,
            created_at INTEGER NOT NULL
        )
    )";

    // 文档表保存知识库中的文档记录。
    const QString createDocuments = R"(
        CREATE TABLE IF NOT EXISTS documents (
            id TEXT PRIMARY KEY,
            kb_id TEXT NOT NULL,
            filename TEXT NOT NULL,
            status TEXT NOT NULL,
            size INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL
        )
    )";

    // 设置表用于保存客户端本地配置。
    const QString createSettings = R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
    )";

    // 统一复用同一个连接执行建表语句，避免跨连接状态不一致。
    QSqlQuery query(db_);
    if (!query.exec(createSessions)) {
        qWarning() << "[DB] create sessions failed: " << query.lastError().text();
        return false;
    }
    if (!query.exec(createMessages)) {
        qWarning() << "[DB] create messages failed:" << query.lastError().text();
        return false;
    }
    if (!query.exec(createDocuments)) {
        qWarning() << "[DB] create documents failed:" << query.lastError().text();
        return false;
    }
    if (!query.exec(createSettings)) {
        qWarning() << "[DB] create settings failed:" << query.lastError().text();
        return false;
    }

    // 所有基础表创建成功后，客户端即可安全启动。
    qDebug() << "[DB] schema initialized";
    return true;
}
