//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "models/chunk_record.h"
#include <string>
#include <vector>
struct sqlite3;

// 负责 chunks 表的读写操作。
class ChunkRepository {
public:
    // db 由外部数据库对象提供，这里只保存裸指针引用。
    explicit ChunkRepository(sqlite3 *db);

    // 插入一条文本分块记录。
    void insert(const ChunkRecord &chunk);

    // 根据文档 ID 获取其全部分块，按顺序返回。
    std::vector<ChunkRecord> list_by_doc_id(const std::string &doc_id);

    // 根据文档 ID 删除chunk
    void remove_by_doc_id(const std::string &doc_id);
private:
    // 底层 sqlite3 连接句柄。
    sqlite3 *db_;
};
