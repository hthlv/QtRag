//
// Created by hyp on 2026/3/26.
//

#include "llm_client.h"
#include <sstream>
#include <algorithm>

namespace {
    // 截断文本，避免回答太长
    std::string truncate_text(const std::string &text, std::size_t max_len) {
        if (text.size() <= max_len) {
            return text;
        }
        return text.substr(0, max_len) + "...";
    }
}


std::string LLMClient::generate(const std::string &query,
                                const std::vector<RetrievedChunk> &contexts,
                                const std::string &prompt) const {
    (void)prompt;   // 先不用，后续接入真正LLM再使用
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
