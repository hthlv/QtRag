//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "http_transport.h"
#include "core/retriever.h"
#include <string>
#include <vector>
#include <functional>

// LLMClient：上游大模型 provider 抽象。
class LLMClient {
public:
    // 通过基类指针管理不同 provider 时必须有虚析构。
    virtual ~LLMClient() = default;

    // 非流式：输入问题、检索上下文和 prompt，生成回答
    virtual std::string generate(const std::string &query,
                                 const std::vector<RetrievedChunk> &contexts,
                                 const std::string &prompt) const = 0;

    // 流式输出必须由具体 provider 自己实现，禁止回退到伪流式。
    virtual void stream_generate(const std::string &query,
                                 const std::vector<RetrievedChunk> &contexts,
                                 const std::string &prompt,
                                 const std::function<void(const std::string &)> &on_chunk) const = 0;
};

// Ollama 文本生成实现，兼容现有 /api/generate。
class OllamaLLMClient final : public LLMClient {
public:
    // Ollama 走统一 base_url，便于和 OpenAI 共用同一套配置结构。
    OllamaLLMClient(const std::string &base_url,
                    const std::string &model,
                    int timeout_ms);

    // 非流式生成直接对应 Ollama /api/generate。
    std::string generate(const std::string &query,
                         const std::vector<RetrievedChunk> &contexts,
                         const std::string &prompt) const override;

    // 流式生成直接消费 Ollama 的 NDJSON 增量。
    void stream_generate(const std::string &query,
                         const std::vector<RetrievedChunk> &contexts,
                         const std::string &prompt,
                         const std::function<void(const std::string &)> &on_chunk) const override;

private:
    // 统一复用 JSON/HTTP 传输层。
    HttpTransport transport_;
    std::string model_;
};

// OpenAI 文本生成实现，同时兼容 Responses API 和 Chat Completions API。
class OpenAILLMClient final : public LLMClient {
public:
    // OpenAI 需要默认鉴权头、聊天接口风格、store 选项和 reasoning 参数。
    OpenAILLMClient(const std::string &base_url,
                    const std::string &api_key,
                    const std::string &model,
                    int timeout_ms,
                    const std::string &chat_api,
                    bool store,
                    const std::string &reasoning_effort,
                    const std::string &organization,
                    const std::string &project);

    // 非流式 OpenAI 回答从 Responses API 的 output 结构中提取文本。
    std::string generate(const std::string &query,
                         const std::vector<RetrievedChunk> &contexts,
                         const std::string &prompt) const override;

    // 流式 OpenAI 直接消费上游 SSE 事件，再映射成当前系统的 token 回调。
    void stream_generate(const std::string &query,
                         const std::vector<RetrievedChunk> &contexts,
                         const std::string &prompt,
                         const std::function<void(const std::string &)> &on_chunk) const override;

private:
    // Authorization、Organization、Project 等头在 transport 构造阶段注入。
    HttpTransport transport_;
    std::string model_;
    // 当前聊天协议风格：responses / chat_completions。
    std::string chat_api_;
    // 是否允许 OpenAI 存储响应，默认建议关闭。
    bool store_{false};
    // reasoning effort 为可选参数，只有配置时才带上。
    std::string reasoning_effort_;
};
