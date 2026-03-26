//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <QString>
#include <QtGlobal>

// 对应 sessions 表的一条会话记录。
struct SessionRecord {
    // 会话唯一 ID。
    QString id;

    // 会话标题（通常由首条用户问题生成）。
    QString title;

    // 当前会话绑定的知识库 ID。
    QString kb_id;

    // 会话创建时间戳（毫秒/秒由上层统一约定）。
    qint64 created_at{0};

    // 会话最近更新时间戳。
    qint64 updated_at{0};
};
