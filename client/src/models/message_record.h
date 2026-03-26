//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <QString>
#include <QtGlobal>

// 对应 messages 表的一条消息记录。
struct MessageRecord {
    // 消息唯一 ID。
    QString id;

    // 所属会话 ID。
    QString session_id;

    // 角色，例如 user / assistant / system。
    QString role;

    // 消息正文。
    QString content;

    // 消息状态，例如 pending / done / error。
    QString status;

    // 消息创建时间戳。
    qint64 created_at{0};
};
