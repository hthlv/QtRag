//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "models/document_record.h"
#include <optional>
#include <string>
#include <vector>
struct sqlite3;

// 负责 documents 表的增删改查操作。
class DocumentRepository {
public:
    // db 由外部 SqlliteStore 持有，这里只做非拥有式引用。
    explicit DocumentRepository(sqlite3 *db);

    // 插入一条新的文档记录。
    void insert(const DocumentRecord &doc);

    // 更新文档状态、分块数量以及更新时间。
    void update_status_and_chunk_count(const std::string &id,
                                       const std::string &status,
                                       int chunk_count,
                                       std::int64_t updated_at);

    // 按文档 ID 查询；不存在时返回空。
    std::optional<DocumentRecord> find_by_id(const std::string &id);

    // 获取所有文档，按创建时间倒序返回。
    std::vector<DocumentRecord> list_all();
    // 根据文档 ID 删除文档
    void remove_by_id(const std::string &id);
private:
    // 底层 sqlite3 连接句柄，不负责其生命周期。
    sqlite3 *db_;
};
