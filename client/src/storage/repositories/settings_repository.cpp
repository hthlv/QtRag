//
// Created by hyp on 2026/3/26.
//

#include "settings_repository.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

SettingsRepository::SettingsRepository(const QSqlDatabase &db)
    :db_(db){
}

bool SettingsRepository::setValue(const QString &key, const QString &value) {
    // 利用 ON CONFLICT 做 UPSERT，避免先查后改。
    QSqlQuery query(db_);
    query.prepare(R"(
        INSERT INTO settings (key, value)
        VALUES (?, ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value
    )");
    query.addBindValue(key);
    query.addBindValue(value);
    if (!query.exec()) {
        qWarning() << "set setting failed:" << query.lastError().text();
        return false;
    }
    return true;
}

std::optional<QString> SettingsRepository::getValue(const QString &key) {
    QSqlQuery query(db_);

    // 按 key 精确查询一条配置值。
    query.prepare(R"(
        SELECT value FROM settings WHERE key = ? LIMIT 1
    )");
    query.addBindValue(key);
    if (!query.exec()) {
        qWarning() << "get setting failed:" << query.lastError().text();
        return std::nullopt;
    }
    if (query.next()) {
        return query.value(0).toString();
    }
    return std::nullopt;
}
