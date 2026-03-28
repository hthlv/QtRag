//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "models/session_record.h"
#include <QSqlDatabase>
#include <QVector>
#include <optional>

// 负责 sessions 表的读写操作。
class SessionRepository {
public:
    // db 由外部传入并管理生命周期，这里只保存连接句柄副本。
    explicit SessionRepository(const QSqlDatabase &db);

    // 插入会话记录。
    bool insert(const SessionRecord &session);

    // 删除会话
    bool removeById(const QString &sessionId);

    // 获取全部会话，按更新时间倒序。
    QVector<SessionRecord> listAll();

    // 按 ID 查询会话，不存在时返回空。
    std::optional<SessionRecord> findById(const QString &id);

    // 更新会话更新时间
    bool touch(const QString &id, qint64 updatedAt);

    // 更新会话标题和更新时间
    bool updateTitle(const QString &id, const QString &title, qint64 updatedAt);

private:
    // 当前仓储使用的数据库连接。
    QSqlDatabase db_;
};
