//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <QString>
#include <QtGlobal>

// 对应 documents 表的一条文档记录。
struct DocumentRecord {
    // 文档唯一 ID。
    QString id;

    // 文档所属知识库 ID。
    QString kb_id;

    // 原始文件名。
    QString filename;

    // 文档处理状态。
    QString status;

    // 文档大小（字节）。
    qint64 size{0};

    // 文档创建时间戳。
    qint64 created_at{0};
};
