//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "models/message_record.h"
#include <QSqlDatabase>
#include <QVector>

// 负责 messages 表的读写操作。
class MessageRepository {
public:
    // db 由外部连接工厂提供。
    explicit MessageRepository(const QSqlDatabase &db);

    // 插入单条消息。
    bool insert(const MessageRecord &message);

    // 按会话 ID 获取消息列表，按创建时间升序。
    QVector<MessageRecord> listBySessionId(const QString &sessionId);

private:
    // 当前仓储使用的数据库连接。
    QSqlDatabase db_;
};
