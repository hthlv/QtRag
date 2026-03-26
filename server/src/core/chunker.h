//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <vector>
#include <string>

// 文本切片器：负责把一段长文本切成多个chunk
class Chunker {
public:
    // chunk_size：每个 chunk 的最大字符数
    // overlap：相邻 chunk 之间重叠的字符数
    Chunker(std::size_t chunk_size, std::size_t overlap);
    // 将输入文本切成多个 chunk
    std::vector<std::string> split(const std::string &text) const;
private:
    std::size_t chunk_size_;
    std::size_t overlap_;
};