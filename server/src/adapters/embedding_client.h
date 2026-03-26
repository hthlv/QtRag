//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <vector>
#include <string>

// EmbeddingClient：负责把文本转换成固定维度向量
class EmbeddingClient {
public:
    // 返回文本的 embedding 向量
    std::vector<float> embed(const std::string &text) const;
    // 返回向量维度
    std::size_t dimension() const { return kDimension; }
private:
    static constexpr std::size_t kDimension = 128;
};