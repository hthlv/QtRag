//
// Created by hyp on 2026/3/26.
//

#include "chunker.h"
#include <algorithm>
#include <stdexcept>

namespace {
    // 根据 UTF-8 首字节推断当前字符的字节长度；非法起始字节按 1 字节兜底处理。
    std::size_t utf8_char_length(unsigned char lead) {
        if ((lead & 0x80) == 0x00) {
            return 1;
        }
        if ((lead & 0xE0) == 0xC0) {
            return 2;
        }
        if ((lead & 0xF0) == 0xE0) {
            return 3;
        }
        if ((lead & 0xF8) == 0xF0) {
            return 4;
        }
        return 1;
    }

    // 预计算每个 UTF-8 字符在原始字节串中的起始位置，便于后续按“字符数”切片。
    std::vector<std::size_t> build_utf8_char_offsets(const std::string &text) {
        std::vector<std::size_t> offsets;
        offsets.reserve(text.size() + 1);
        std::size_t i = 0;
        while (i < text.size()) {
            offsets.push_back(i);
            const std::size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
            i += std::min(char_len, text.size() - i);
        }
        offsets.push_back(text.size());
        return offsets;
    }
}

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
    if (text.empty()) {
        return result;
    }

    const auto offsets = build_utf8_char_offsets(text);
    const std::size_t total_chars = offsets.size() - 1;
    std::size_t start_char = 0;
    const std::size_t step_chars = chunk_size_ - overlap_;
    while (start_char < total_chars) {
        // chunk_size_ / overlap_ 的语义是“字符数”，因此先算字符区间，再映射回字节区间。
        const std::size_t end_char = std::min(start_char + chunk_size_, total_chars);
        const std::size_t start_byte = offsets[start_char];
        const std::size_t end_byte = offsets[end_char];
        result.push_back(text.substr(start_byte, end_byte - start_byte));
        if (end_char >= total_chars) {
            break;
        }
        start_char += step_chars;
    }

    return result;
}
