//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "core/retriever.h"
#include <string>
#include <vector>
#include <functional>

class LLMClient {
public:
    // 非流式：输入问题、检索上下文和 prompt，生成回答
    std::string generate(const std::string &query,
                         const std::vector<RetrievedChunk> &contexts,
                         const std::string &prompt) const;
    // 流式：通过回调逐段返回文本
    void stream_generate(const std::string &query,
        const std::vector<RetrievedChunk> &contexts,
        const std::string &prompt,
        const std::function<void(const std::string&)> &on_chunk) const;
};
