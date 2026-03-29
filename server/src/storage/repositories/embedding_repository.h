//
// Created by hyp on 2026/3/27.
//

#pragma once
#include "models/embedding_record.h"
#include <vector>
struct sqlite3;
class EmbeddingRepository {
public:
    explicit EmbeddingRepository(sqlite3 *db);
    // 插入或覆盖一条 embedding 记录
    void insert_or_replace(const EmbeddingRecord &record);
    // 读取全部 embedding 记录，用于服务端启动时恢复索引
    std::vector<EmbeddingRecord> list_all();
    // 根据 chunk_id 删除embedding
    void remove_by_chunk_id(const std::string &chunk_id);
private:
    sqlite3 *db_;
};
