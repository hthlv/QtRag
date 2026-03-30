//
// Created by hyp on 2026/3/26.
//

#include "in_memory_vector_store.h"
#include <algorithm>
#include <cmath>
#include <queue>
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
    // embedding 全量重建后，先把旧索引彻底清空，再把旧容量一并交还给分配器。
    std::vector<VectorStoreEntry>().swap(entries_);
}

std::vector<VectorSearchHit> InMemoryVectorStore::search(
    const std::vector<float> &query_embedding,
    std::size_t top_k) const {
    std::vector<VectorSearchHit> hits;
    if (query_embedding.empty() || top_k == 0) {
        return hits;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    struct Candidate {
        float score{0.0f};
        const VectorStoreEntry *entry{nullptr};
    };
    struct CandidateGreater {
        bool operator()(const Candidate &lhs, const Candidate &rhs) const {
            return lhs.score > rhs.score;
        }
    };

    // 暴力检索仍然遍历全部向量，但只维护 top-k 候选，避免把整库内容复制进临时 hits。
    std::priority_queue<Candidate, std::vector<Candidate>, CandidateGreater> top_hits;
    for (const auto &entry: entries_) {
        if (entry.embedding.size() != query_embedding.size()) {
            continue;
        }
        const float score = cosine_similarity(entry.embedding, query_embedding);
        if (top_hits.size() < top_k) {
            top_hits.push({score, &entry});
            continue;
        }
        if (score <= top_hits.top().score) {
            continue;
        }
        top_hits.pop();
        top_hits.push({score, &entry});
    }

    hits.reserve(top_hits.size());
    while (!top_hits.empty()) {
        const Candidate candidate = top_hits.top();
        top_hits.pop();

        VectorSearchHit hit;
        hit.chunk_id = candidate.entry->chunk_id;
        hit.doc_id = candidate.entry->doc_id;
        hit.content = candidate.entry->content;
        hit.score = candidate.score;
        hits.push_back(std::move(hit));
    }

    // 堆里弹出的是从低到高，这里统一反转成最终返回顺序。
    std::sort(hits.begin(), hits.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.score > rhs.score;
    });
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
