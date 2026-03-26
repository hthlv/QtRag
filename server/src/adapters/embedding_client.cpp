//
// Created by hyp on 2026/3/26.
//

#include "embedding_client.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {
    // 将 token 映射到向量的某个为止
    void add_token_to_vector(const std::string &token, std::vector<float> &vec) {
        if (token.empty()) {
            return;
        }
        const std::size_t dim = vec.size();
        // 使用 std::hash 对 token 做哈希
        std::size_t h = std::hash<std::string>{}(token);
        // 落到某个维度槽位
        std::size_t index = h % dim;
        // 为了让分布更丰富，再用一位决定加正还是加负
        float sign = ((h >> 1) & 1) ? 1.0f : -1.0f;
        vec[index] += sign;
    }

    // 对向量做 L2 归一化
    void normalize(std::vector<float> &vec) {
        double sum = 0.0;
        for (float v: vec) {
            sum += static_cast<double>(v) * static_cast<double>(v);
        }
        if (sum <= 1e-12) {
            return;
        }
        double norm = std::sqrt(sum);
        for (float &v : vec) {
            v = static_cast<float>(v / norm);
        }
    }
}

std::vector<float> EmbeddingClient::embed(const std::string &text) const {
    // 先初始化一个固定维度的全0向量
    std::vector<float> vec(kDimension, 0.0f);
    // 1. 按“字母数字连续串”切 token
    // 2. 每个 token hash 到一个槽位
    // 3. 最后做归一化
    std::string token;
    token.reserve(32);
    auto flush_token = [&]() {
        if (!token.empty()) {
            add_token_to_vector(token, vec);
            token.clear();
        }
    };
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            token.push_back(static_cast<char>(std::tolower(ch)));
        } else {
            flush_token();
        }
    }
    flush_token();
    // 如果整段文本都没有 token （例如全是符号）
    // 那就退化成按字符做一点简单映射，避免全零向量
    bool all_zero = true;
    for (float v : vec) {
        if (std::abs(v) > 1e-6f) {
            all_zero = false;
            break;
        }
    }
    if (all_zero) {
        for (unsigned char ch : text) {
            std::size_t index = ch % vec.size();
            vec[index] += 1.0f;
        }
    }
    // 最后归一化，方便后续直接用点积近似余弦相似度
    normalize(vec);
    return vec;
}
