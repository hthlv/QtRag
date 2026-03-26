//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <cstdint>
#include <string>

// 对应 documents 表的一条文档记录。
struct DocumentRecord {
    // 文档唯一标识。
    std::string id;

    // 所属知识库 ID。
    std::string kb_id;

    // 原始文件名。
    std::string filename;

    // 文件在磁盘上的路径。
    std::string file_path;

    // 文档处理状态，例如 uploaded / chunked。
    std::string status;

    // 当前文档已经生成的分块数量。
    int chunk_count{0};

    // 创建时间戳。
    std::int64_t created_at{0};

    // 最后更新时间戳。
    std::int64_t updated_at{0};
};
