//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <vector>
#include <string>

// EmbeddingClient：负责把文本转换成固定维度向量
class EmbeddingClient {
public:
    EmbeddingClient(const std::string& host,
                    const std::string& port,
                    const std::string& model,
                    int timeout_ms);

    // 返回文本的 embedding 向量
    std::vector<float> embed(const std::string &text) const;

private:
    std::string host_;
    std::string port_;
    std::string model_;
    int timeout_ms_{30000};
};
