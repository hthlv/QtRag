//
// Created by hyp on 2026/3/27.
//

#pragma once
#include <string>
#include <vector>
#include <cstdint>

class EmbeddingRecord {
public:
    std::string chunk_id;
    std::string doc_id;
    std::string content;
    std::vector<float> embedding;
    std::int64_t created_at{0};
};
