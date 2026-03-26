//
// Created by hyp on 2026/3/26.
//

#include "chunk_repository.h"
#include <sqlite3.h>
#include <stdexcept>

ChunkRepository::ChunkRepository(sqlite3 *db)
    :db_(db){
}

void ChunkRepository::insert(const ChunkRecord &chunk) {
    // 通过预编译语句写入分块内容和顺序信息。
    const char* sql = R"(
        INSERT INTO chunks (
            id, doc_id, chunk_index, content, created_at
        ) VALUES (?, ?, ?, ?, ?)
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("prepare insert chunk failed");
    }
    sqlite3_bind_text(stmt, 1, chunk.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, chunk.doc_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, chunk.chunk_index);
    sqlite3_bind_text(stmt, 4, chunk.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, chunk.created_at);

    // 成功执行后应该返回 SQLITE_DONE。
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("insert chunk failed");
    }
    sqlite3_finalize(stmt);
}

std::vector<ChunkRecord> ChunkRepository::list_by_doc_id(const std::string &doc_id) {
    // 按 chunk_index 升序读取，确保还原后的文本顺序稳定。
    const char *sql = R"(
        SELECT id, doc_id, chunk_index, content, created_at
        FROM chunks
        WHERE doc_id = ?
        ORDER BY chunk_index ASC
    )";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("prepare list chunks failed");
    }
    sqlite3_bind_text(stmt, 1, doc_id.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<ChunkRecord> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChunkRecord chunk;

        // 按 SELECT 字段顺序把当前行拷贝到 ChunkRecord。
        chunk.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        chunk.doc_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        chunk.chunk_index = sqlite3_column_int(stmt, 2);
        chunk.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        chunk.created_at = sqlite3_column_int64(stmt, 4);
        result.push_back(std::move(chunk));
    }
    sqlite3_finalize(stmt);
    return result;
}
