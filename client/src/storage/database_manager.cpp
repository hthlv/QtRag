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
    static const QString connectionName = "qtrag_connection";

    if (QSqlDatabase::contains(connectionName)) {
        db_ = QSqlDatabase::database(connectionName);
    } else {
        db_ = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db_.setDatabaseName(dbPath_);
    }

    if (!db_.open()) {
        qWarning() << "[DB] open failed" << db_.lastError().text();
        return false;
    }
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
    const QString createSessions = R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            kb_id TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        )
    )";
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
    const QString createSettings = R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL
        )
    )";
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
    qDebug() << "[DB] schema initialized";
    return true;
}
