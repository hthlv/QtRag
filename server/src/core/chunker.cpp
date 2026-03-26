//
// Created by hyp on 2026/3/26.
//

#include "chunker.h"
#include <algorithm>
#include <stdexcept>

Chunker::Chunker(std::size_t chunk_size, std::size_t overlap)
    : chunk_size_(chunk_size), overlap_(overlap) {
    if (chunk_size_ == 0) {
        throw std::invalid_argument("chunk_size must be greater than 0");
    }
    if (overlap_ >= chunk_size_) {
        throw std::invalid_argument("overlap must be smaller than the chunk size");
    }
}

std::vector<std::string> Chunker::split(const std::string &text) const {
    std::vector<std::string> result;
    // 空文本直接返回
    if (text.empty())   return result;
    std::size_t start = 0;
    const std::size_t step = chunk_size_ - overlap_;
    while (start < text.size()) {
        // 当前 chunk 的结束位置
        const std::size_t end = std::min(start + chunk_size_, text.size());
        // 截取当前 chunk
        result.push_back(text.substr(start, end - start));
        // 如果已经到结尾，结束循环
        if (end >= text.size())    break;
        // 下一块的开始位置
        start += step;
    }

    return result;
}
