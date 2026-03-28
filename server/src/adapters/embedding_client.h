//
// Created by hyp on 2026/3/26.
//

#pragma once
#include "http_transport.h"
#include <vector>
#include <string>

// EmbeddingClient：上游 embedding provider 抽象。
class EmbeddingClient {
public:
    // 通过虚析构保证通过基类指针释放派生类时行为正确。
    virtual ~EmbeddingClient() = default;

    // 返回文本的 embedding 向量
    virtual std::vector<float> embed(const std::string &text) const = 0;
};

// Ollama embedding 实现，兼容现有 /api/embed。
class OllamaEmbeddingClient final : public EmbeddingClient {
public:
    // base_url 支持把原来的 host/port 配置折叠成统一地址。
    OllamaEmbeddingClient(const std::string &base_url,
                          const std::string &model,
                          int timeout_ms);

    // 对外统一暴露 embed 接口，内部再转成 Ollama 请求格式。
    std::vector<float> embed(const std::string &text) const override;

private:
    // 统一复用底层 HTTP 传输逻辑。
    HttpTransport transport_;
    std::string model_;
};

// OpenAI embedding 实现，调用 /v1/embeddings。
class OpenAIEmbeddingClient final : public EmbeddingClient {
public:
    // OpenAI provider 需要额外携带 API Key、组织和项目头。
    OpenAIEmbeddingClient(const std::string &base_url,
                          const std::string &api_key,
                          const std::string &model,
                          int timeout_ms,
                          int dimensions,
                          const std::string &organization,
                          const std::string &project);

    // 对外接口和 Ollama 保持一致，便于检索层透明切换 provider。
    std::vector<float> embed(const std::string &text) const override;

private:
    // 对 OpenAI 来说，Authorization 等默认头在 transport 构造时一次性注入。
    HttpTransport transport_;
    std::string model_;
    // 可选裁剪 embedding 维度；<= 0 时表示不传该字段。
    int dimensions_{0};
};
