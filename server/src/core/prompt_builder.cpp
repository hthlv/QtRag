//
// Created by hyp on 2026/3/26.
//

#include "prompt_builder.h"
#include <sstream>

std::string PromptBuilder::build(const std::string &query, const std::vector<RetrievedChunk> &contexts) const {
    std::ostringstream oss;
    oss << "你是一个文档问答助手，请严格依据提供的上下文回答问题。\n";
    oss << "如果上下文中没有足够信息，请明确说“我无法从知识库中找到答案”。\n\n";
    oss << "【上下文】\n";
    for (std::size_t i = 0; i < contexts.size(); ++i) {
        oss << "[" << (i + 1) << "] ";
        if (!contexts[i].filename.empty()) {
            oss << "来源文件: " << contexts[i].filename << "\n";
        }
        oss << contexts[i].text << "\n\n";
    }
    oss << "【问题】\n";
    oss << query << "\n\n";
    oss << "【回答要求】\n";
    oss << "1. 尽量简洁清晰\n";
    oss << "2. 优先总结上下文中的关键信息\n";
    oss << "3. 不要编造文档中不存在的内容\n";
    return oss.str();
}
