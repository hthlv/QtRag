//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "core/retriever.h"
#include <string>
#include <vector>

class LLMClient {
public:
    // 输入问题、检索上下文和 prompt，生成回答
    std::string generate(const std::string &query,
                         const std::vector<RetrievedChunk> &contexts,
                         const std::string &prompt) const;
};
