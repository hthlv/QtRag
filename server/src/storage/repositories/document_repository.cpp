//
// Created by hyp on 2026/3/26.
//

#include "document_repository.h"
#include <sqlite3.h>
#include <stdexcept>

DocumentRepository::DocumentRepository(sqlite3 *db)
    : db_(db) {
}

void DocumentRepository::insert(const DocumentRecord &doc) {
    // 使用预编译语句插入文档，避免手工拼接 SQL。
    const char *sql = R"(
        INSERT INTO documents (
            id, kb_id, filename, file_path, status, chunk_count, created_at, updated_at
        ) VALUES(?, ?, ?, ?, ?, ?, ?, ?)
    )";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("prepare insert document failed");
    }
    sqlite3_bind_text(stmt, 1, doc.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, doc.kb_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, doc.filename.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, doc.file_path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, doc.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, doc.chunk_count);
    sqlite3_bind_int64(stmt, 7, doc.created_at);
    sqlite3_bind_int64(stmt, 8, doc.updated_at);

    // 只有执行到 SQLITE_DONE 才表示插入完成。
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("insert document failed");
    }
    sqlite3_finalize(stmt);
}

void DocumentRepository::update_status_and_chunk_count(const std::string &id, const std::string &status, int chunk_count,
                                                       std::int64_t updated_at) {
    // 文档分块完成后，会通过这条语句回写状态和统计信息。
    const char *sql = R"(
        UPDATE documents
        SET status = ?, chunk_count = ?, updated_at = ?
        WHERE id = ?
    )";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("prepare update document failed");
    }
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, chunk_count);
    sqlite3_bind_int64(stmt, 3, updated_at);
    sqlite3_bind_text(stmt, 4, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("update document failed");
    }
    sqlite3_finalize(stmt);
}

std::optional<DocumentRecord> DocumentRepository::find_by_id(const std::string &id) {
    // 精确查询单条文档记录。
    const char *sql = R"(
        SELECT id, kb_id, filename, file_path, status, chunk_count, created_at, updated_at
        FROM documents
        WHERE id = ?
        LIMIT 1
    )";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("prepare find document failed");
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        DocumentRecord doc;

        // 按 SELECT 字段顺序把结果集映射回内存对象。
        doc.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        doc.kb_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        doc.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        doc.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        doc.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        doc.chunk_count = sqlite3_column_int(stmt, 5);
        doc.created_at = sqlite3_column_int64(stmt, 6);
        doc.updated_at = sqlite3_column_int64(stmt, 7);
        sqlite3_finalize(stmt);
        return doc;
    }
    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::vector<DocumentRecord> DocumentRepository::list_all() {
    // 列出全部文档，方便管理页或调试接口直接展示。
    const char *sql = R"(
        SELECT id, kb_id, filename, file_path, status, chunk_count, created_at, updated_at
        FROM documents
        ORDER BY created_at DESC
    )";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("prepare list document failed");
    }
    std::vector<DocumentRecord> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DocumentRecord doc;

        // 每一步循环读取一条记录并追加到结果数组。
        doc.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        doc.kb_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        doc.filename = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        doc.file_path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        doc.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        doc.chunk_count = sqlite3_column_int(stmt, 5);
        doc.created_at = sqlite3_column_int64(stmt, 6);
        doc.updated_at = sqlite3_column_int64(stmt, 7);
        result.push_back(std::move(doc));
    }
    sqlite3_finalize(stmt);
    return result;
}
