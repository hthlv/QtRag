//
// Created by hyp on 2026/3/26.
//

#include "retriever.h"
#include "adapters/embedding_client.h"
#include "core/in_memory_vector_store.h"
#include "storage/repositories/document_repository.h"

Retriever::Retriever(const EmbeddingClient *embedding_client,
                     const InMemoryVectorStore *vector_store,
                     sqlite3 *db,
                     std::mutex *db_mutex) : embedding_client_(embedding_client),
                                             vector_store_(vector_store),
                                             db_(db),
                                             db_mutex_(db_mutex) {
}

std::vector<RetrievedChunk> Retriever::retrieve(const std::string &query, std::size_t top_k) const {
    std::vector<RetrievedChunk> result;
    // 空查询直接返回结果
    if (query.empty() || top_k == 0) {
        return result;
    }
    // 1. 将 query 转成 embedding
    const auto query_embedding = embedding_client_->embed(query);
    // 2. 去向量索引中检索
    const auto hits = vector_store_->search(query_embedding, top_k);
    // 3. 补全文档等信息
    // 给数据库加锁保护
    std::lock_guard<std::mutex> lock(*db_mutex_);
    DocumentRepository docRepo(db_);
    for (const auto &hit: hits) {
        RetrievedChunk item;
        item.chunk_id = hit.chunk_id;
        item.doc_id = hit.doc_id;
        item.text = hit.content;
        item.score = hit.score;
        // 根据 doc_id 查文档名
        auto doc = docRepo.find_by_id(hit.doc_id);
        if (doc.has_value()) {
            item.filename = doc->filename;
        } else {
            item.filename = "";
        }
        result.push_back(item);
    }

    return result;
}
