//
// Created by hyp on 2026/3/26.
//

#include "llm_client.h"
#include <sstream>
#include <algorithm>
#include <bits/this_thread_sleep.h>

namespace {
    // 截断文本，避免回答太长
    std::string truncate_text(const std::string &text, std::size_t max_len) {
        if (text.size() <= max_len) {
            return text;
        }
        return text.substr(0, max_len) + "...";
    }

    // 根据 UTF-8 首字节判断当前字符总共占多少字节。
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

    // 按“字符数”而不是“字节数”切分 UTF-8 文本，避免中文被截断后出现乱码。
    std::vector<std::string> split_utf8_by_chars(const std::string &text, std::size_t max_chars) {
        std::vector<std::string> pieces;
        if (max_chars == 0 || text.empty()) {
            return pieces;
        }

        std::size_t start = 0;
        std::size_t char_count = 0;
        std::size_t i = 0;
        while (i < text.size()) {
            const std::size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
            if (char_count == max_chars) {
                // 已累计满一个分片时，从上一个合法边界切出这一段。
                pieces.push_back(text.substr(start, i - start));
                start = i;
                char_count = 0;
            }
            i += std::min(char_len, text.size() - i);
            ++char_count;
        }

        if (start < text.size()) {
            pieces.push_back(text.substr(start));
        }
        return pieces;
    }
}


std::string LLMClient::generate(const std::string &query,
                                const std::vector<RetrievedChunk> &contexts,
                                const std::string &prompt) const {
    (void) prompt; // 先不用，后续接入真正LLM再使用
    // 如果没有检索到上下文，就返回无法回答
    if (contexts.empty()) {
        return "我无法从当前知识库中找到足够相关的内容来回答这个问题。";
    }
    std::ostringstream oss;
    // 这里用“基于检索结果的模板回答”模拟 LLM 输出
    oss << "根据知识库中检索到的内容，关于“" << query << "”的问题，我整理出以下信息：\n\n";
    // 最多选前 2~3 个片段做一个简要总结
    const std::size_t max_items = std::min<std::size_t>(contexts.size(), 3);
    for (std::size_t i = 0; i < max_items; ++i) {
        oss << (i + 1) << ". ";
        if (!contexts[i].filename.empty()) {
            oss << "在文件《" << contexts[i].filename << "》中提到：\n";
        }
        oss << truncate_text(contexts[i].text, 180) << "\n\n";
    }
    oss << "如果你希望，我还可以继续基于这些文档做更具体的总结、对比或条目化解释。";
    return oss.str();
}

void LLMClient::stream_generate(const std::string &query,
                                const std::vector<RetrievedChunk> &contexts,
                                const std::string &prompt,
                                const std::function<void(const std::string &)> &on_chunk) const {
    // 1. 先生成完整回答
    // 2. 再按字符数分段，避免把 UTF-8 中文切坏
    const std::string full_answer = generate(query, contexts, prompt);
    const std::size_t piece_size = 18;
    // 这里先预切片，后续逐段回调给 SSE 层发送。
    const auto pieces = split_utf8_by_chars(full_answer, piece_size);
    for (const auto &piece: pieces) {
        // 通过回调把当前片段交给上层
        on_chunk(piece);
        // 人工 sleep 一下，模拟流式输出效果
        // 注意：当前服务器还是单线程同步版，这会阻塞其它请求
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
}
