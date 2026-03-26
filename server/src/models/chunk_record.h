//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <cstdint>
#include <string>

// 对应 chunks 表的一条文本分块记录。
struct ChunkRecord {
    // 分块唯一标识。
    std::string id;

    // 所属文档 ID。
    std::string doc_id;

    // 分块在文档中的顺序编号。
    int chunk_index{0};

    // 分块文本内容。
    std::string content;

    // 分块创建时间戳。
    std::int64_t created_at{0};
};
