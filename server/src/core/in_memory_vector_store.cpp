//
// Created by hyp on 2026/3/26.
//

#include "in_memory_vector_store.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

void InMemoryVectorStore::add(const std::string &chunk_id,
                              const std::string &doc_id,
                              const std::string &content,
                              const std::vector<float> &embedding) {
    if (embedding.empty()) {
        throw std::invalid_argument("embedding must not be empty");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    VectorStoreEntry entry;
    entry.chunk_id = chunk_id;
    entry.doc_id = doc_id;
    entry.content = content;
    entry.embedding = embedding;
    entries_.push_back(std::move(entry));
}

void InMemoryVectorStore::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    // embedding 全量重建后，先把旧索引彻底清空，再按数据库最新记录恢复。
    entries_.clear();
}

std::vector<VectorSearchHit> InMemoryVectorStore::search(
    const std::vector<float> &query_embedding,
    std::size_t top_k) const {
    std::vector<VectorSearchHit> hits;
    if (query_embedding.empty() || top_k == 0) {
        return hits;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // 暴力检索：遍历全部向量，计算相似度
    for (const auto &entry: entries_) {
        if (entry.embedding.size() != query_embedding.size()) {
            continue;
        }
        VectorSearchHit hit;
        hit.chunk_id = entry.chunk_id;
        hit.doc_id = entry.doc_id;
        hit.content = entry.content;
        hit.score = cosine_similarity(entry.embedding, query_embedding);
        hits.push_back(hit);
    }
    // 按相似度从高到底排序
    std::sort(hits.begin(), hits.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.score > rhs.score;
    });
    // 截断到 top-k
    if (hits.size() > top_k) {
        hits.resize(top_k);
    }
    return hits;
}

std::size_t InMemoryVectorStore::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

float InMemoryVectorStore::dot_product(const std::vector<float> &a,
                                       const std::vector<float> &b) {
    if (a.size() != b.size()) {
        return 0.0f;
    }
    float result = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        result += a[i] * b[i];
    }

    return result;
}

float InMemoryVectorStore::vector_norm(const std::vector<float> &values) {
    if (values.empty()) {
        return 0.0f;
    }
    return std::sqrt(dot_product(values, values));
}

float InMemoryVectorStore::cosine_similarity(const std::vector<float> &a,
                                             const std::vector<float> &b) {
    if (a.size() != b.size() || a.empty()) {
        return 0.0f;
    }
    const float a_norm = vector_norm(a);
    const float b_norm = vector_norm(b);
    if (a_norm <= 0.0f || b_norm <= 0.0f) {
        return 0.0f;
    }
    return dot_product(a, b) / (a_norm * b_norm);
}
