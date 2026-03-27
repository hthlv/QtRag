//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <mutex>
#include <vector>
#include <string>

// 向量索引中的一条记录
struct VectorStoreEntry {
    std::string chunk_id;
    std::string doc_id;
    std::string content;
    std::vector<float> embedding;
};

// 检索命中的结果
struct VectorSearchHit {
    std::string chunk_id;
    std::string doc_id;
    std::string content;
    float score{0.0f};
};

// InMemoryVectorStore：最简单的内存向量索引
class InMemoryVectorStore {
public:
    // 向索引中新增一条向量记录
    void add(const std::string &chunk_id,
             const std::string &doc_id,
             const std::string &content,
             const std::vector<float> &embedding);

    // 给定查询向量，返回 top-k 命中结果
    std::vector<VectorSearchHit> search(const std::vector<float> &query_embedding,
                                        std::size_t top_k) const;

    // 当前索引中的记录数
    std::size_t size() const;

private:
    static float dot_product(const std::vector<float> &a,
                             const std::vector<float> &b);
    static float vector_norm(const std::vector<float> &values);
    static float cosine_similarity(const std::vector<float> &a,
                                   const std::vector<float> &b);

private:
    mutable std::mutex mutex_;
    std::vector<VectorStoreEntry> entries_;
};
