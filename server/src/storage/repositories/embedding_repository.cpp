//
// Created by hyp on 2026/3/27.
//

#include "embedding_repository.h"
#include <sqlite3.h>
#include <sstream>
#include <stdexcept>

namespace {
    // 将 embedding 序列化成逗号分隔字符串
    std::string serialize_embedding(const std::vector<float> &embedding) {
        std::ostringstream oss;
        for (std::size_t i = 0; i < embedding.size(); ++i) {
            if (i > 0) {
                oss << ",";
            }
            oss << embedding[i];
        }
        return oss.str();
    }

    // 将逗号分隔字符串反序列化回 vector<float>
    std::vector<float> deserialize_embedding(const std::string &text) {
        std::vector<float> result;
        std::stringstream ss(text);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                result.push_back(std::stof(item));
            }
        }

        return result;
    }
}

EmbeddingRepository::EmbeddingRepository(sqlite3 *db)
    : db_(db) {
}

void EmbeddingRepository::insert_or_replace(const EmbeddingRecord &record) {
    const char *sql = R"(
        INSERT OR REPLACE INTO chunk_embeddings (
            chunk_id, doc_id, content, embedding, created_at
        ) VALUES (?, ?, ?, ?, ?)
    )";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("prepare insert embedding failed");
    }
    std::string serialized = serialize_embedding(record.embedding);
    sqlite3_bind_text(stmt, 1, record.chunk_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, record.doc_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, record.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, serialized.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, record.created_at);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("insert embedding failed");
    }
    sqlite3_finalize(stmt);
}

std::vector<EmbeddingRecord> EmbeddingRepository::list_all() {
    const char *sql = R"(
        SELECT chunk_id, doc_id, content, embedding, created_at
        FROM chunk_embeddings
        ORDER BY created_at ASC
    )";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("prepare list embeddings failed");
    }
    std::vector<EmbeddingRecord> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EmbeddingRecord record;
        record.chunk_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        record.doc_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        record.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* embedding_text =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        record.embedding = deserialize_embedding(embedding_text ? embedding_text : "");
        record.created_at = sqlite3_column_int64(stmt, 4);
        result.push_back(std::move(record));
    }
    sqlite3_finalize(stmt);
    return result;
}

void EmbeddingRepository::remove_by_chunk_id(const std::string &chunk_id) {
    const char *sql = R"(
        DELETE FROM chunk_embeddings
        WHERE chunk_id = ?;
    )";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error("remove embedding failed");
    }
    sqlite3_bind_text(stmt, 1, chunk_id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("remove embedding failed");
    }
    sqlite3_finalize(stmt);
}
