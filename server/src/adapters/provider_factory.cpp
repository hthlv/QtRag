//
// Created by hyp on 2026/3/28.
//

#include "provider_factory.h"

#include "embedding_client.h"
#include "llm_client.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace {
    // provider_type / url scheme 这类配置统一转小写后再比较。
    std::string to_lower_copy(const std::string &text) {
        std::string lowered = text;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return lowered;
    }

    // OpenAI 的 API Key 只允许从环境变量读取，避免写进配置文件。
    std::string read_required_env(const std::string &env_name) {
        const char *value = std::getenv(env_name.c_str());
        if (value == nullptr || std::string(value).empty()) {
            throw std::runtime_error("missing required environment variable: " + env_name);
        }
        return value;
    }

    // 这里只需要前缀判断，用于识别 https:// 配置。
    bool starts_with(const std::string &text, const std::string &prefix) {
        return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
    }

    // 如果当前构建没有 OpenSSL，就提前阻止 https provider 启动，避免运行时才失败。
    void validate_transport_support(const AppConfig &config) {
#ifndef QTRAG_SERVER_HAS_OPENSSL
        if (starts_with(to_lower_copy(config.provider.base_url), "https://")) {
            throw std::runtime_error("https upstream requires OpenSSL support at build time");
        }
#else
        (void)config;
#endif
    }
}

std::unique_ptr<EmbeddingClient> create_embedding_client(const AppConfig &config) {
    // 统一在工厂里分发 embedding provider，业务层不感知具体厂商。
    const std::string provider_type = to_lower_copy(config.provider.type);
    if (provider_type == "ollama") {
        // 旧版本地 Ollama 路径默认仍然走这里。
        return std::make_unique<OllamaEmbeddingClient>(
            config.provider.base_url,
            config.models.embedding,
            config.provider.timeout_ms);
    }
    if (provider_type == "openai") {
        // OpenAI 在创建阶段就校验 HTTPS 支持和 API Key。
        validate_transport_support(config);
        return std::make_unique<OpenAIEmbeddingClient>(
            config.provider.base_url,
            read_required_env(config.openai.api_key_env),
            config.models.embedding,
            config.provider.timeout_ms,
            config.openai.embedding_dimensions,
            config.openai.organization,
            config.openai.project);
    }
    throw std::invalid_argument("unsupported provider type: " + config.provider.type);
}

std::unique_ptr<LLMClient> create_llm_client(const AppConfig &config) {
    // LLM provider 的选择逻辑和 embedding 保持一致，方便后续继续扩展。
    const std::string provider_type = to_lower_copy(config.provider.type);
    if (provider_type == "ollama") {
        // 现有聊天能力默认仍兼容 Ollama。
        return std::make_unique<OllamaLLMClient>(
            config.provider.base_url,
            config.models.chat,
            config.provider.timeout_ms);
    }
    if (provider_type == "openai") {
        // OpenAI 聊天 provider 会自动读取组织、项目和 reasoning 配置。
        validate_transport_support(config);
        return std::make_unique<OpenAILLMClient>(
            config.provider.base_url,
            read_required_env(config.openai.api_key_env),
            config.models.chat,
            config.provider.timeout_ms,
            config.openai.store,
            config.openai.reasoning_effort,
            config.openai.organization,
            config.openai.project);
    }
    throw std::invalid_argument("unsupported provider type: " + config.provider.type);
}
