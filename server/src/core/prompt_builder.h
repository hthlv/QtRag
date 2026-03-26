//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "core/retriever.h"
#include <vector>
#include <string>

// PromptBuilder：负责把 query 和检索的上下文拼成 prompt
class PromptBuilder {
public:
    // 根据 query 和检索结果构造 prompt
    std::string build(const std::string &query,
                      const std::vector<RetrievedChunk> &contexts) const;
};
