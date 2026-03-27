//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <QSqlDatabase>
#include <QString>
#include <optional>

// 负责 settings 表的键值配置读写。
class SettingsRepository {
public:
    // db 由外部传入并复用。
    explicit SettingsRepository(const QSqlDatabase &db);

    // 写入配置项；若 key 已存在则更新 value。
    bool setValue(const QString& key, const QString &value);

    // 读取配置项；不存在或查询失败时返回空。
    std::optional<QString> getValue(const QString& key);

private:
    // 当前仓储使用的数据库连接。
    QSqlDatabase db_;
};
