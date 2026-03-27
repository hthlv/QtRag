//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <mutex>
struct sqlite3;
class EmbeddingClient;
class InMemoryVectorStore;

// 返回给上层的检索结果
struct RetrievedChunk {
    std::string chunk_id;
    std::string doc_id;
    std::string filename;
    std::string text;
    float score{0.0f};
};

// Retriever：负责执行完整检索流程
class Retriever {
public:
    Retriever(const EmbeddingClient *embedding_client,
              const InMemoryVectorStore *vector_store,
              sqlite3 *db,
              std::mutex *db_mutex);

    // 根据 query 检索 tok-k chunks
    std::vector<RetrievedChunk> retrieve(const std::string &query,
                                         std::size_t top_k) const;

private:
    const EmbeddingClient *embedding_client_;
    const InMemoryVectorStore *vector_store_;
    sqlite3 *db_;
    std::mutex *db_mutex_;
};
