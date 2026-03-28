//
// Created by hyp on 2026/3/26.
//

#include "embedding_client.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
    // 兼容不同 provider 的 embedding 返回结构，统一提取成 float 向量。
    std::vector<float> parse_embedding_vector(const nlohmann::json &json_body) {
        std::vector<float> result;

        // 兼容 Ollama 新接口：embeddings: [[...]]
        if (json_body.contains("embeddings") &&
            json_body["embeddings"].is_array() &&
            !json_body["embeddings"].empty()) {
            for (const auto &value : json_body["embeddings"][0]) {
                result.push_back(value.get<float>());
            }
            return result;
        }

        // 兼容一些旧格式：embedding: [...]
        if (json_body.contains("embedding") && json_body["embedding"].is_array()) {
            for (const auto &value : json_body["embedding"]) {
                result.push_back(value.get<float>());
            }
            return result;
        }

        // OpenAI embeddings 返回：data[0].embedding
        if (json_body.contains("data") &&
            json_body["data"].is_array() &&
            !json_body["data"].empty() &&
            json_body["data"][0].contains("embedding") &&
            json_body["data"][0]["embedding"].is_array()) {
            for (const auto &value : json_body["data"][0]["embedding"]) {
                result.push_back(value.get<float>());
            }
            return result;
        }

        throw std::runtime_error("embedding response format is invalid");
    }
}

OllamaEmbeddingClient::OllamaEmbeddingClient(const std::string &base_url,
                                             const std::string &model,
                                             int timeout_ms)
    // 复用统一 transport，Ollama 仅负责提供路径和请求体差异。
    : transport_(base_url, timeout_ms),
      model_(model) {
}

std::vector<float> OllamaEmbeddingClient::embed(const std::string &text) const {
    // 空文本不请求上游，直接返回空向量。
    if (text.empty()) {
        return {};
    }

    nlohmann::json req_json;
    req_json["model"] = model_;
    try {
        // Ollama /api/embed 支持 input 数组格式。
        req_json["input"] = nlohmann::json::array({text});
    } catch (const nlohmann::json::exception &) {
        throw std::runtime_error("embedding input must be valid UTF-8 text");
    }

    try {
        const HttpResponse response = transport_.post_json("/api/embed", req_json);
        if (response.status_code != 200) {
            throw std::runtime_error("embedding request failed, status=" +
                                     std::to_string(response.status_code) +
                                     ", body=" + response.body);
        }
        // 成功后统一走同一个解析函数，兼容新旧返回体。
        return parse_embedding_vector(nlohmann::json::parse(response.body));
    } catch (const std::runtime_error &ex) {
        // 保留已归类的 embedding 异常，其它异常统一补齐前缀后再抛出。
        const std::string message = ex.what();
        if (message.find("embedding request failed") != std::string::npos ||
            message.find("embedding response format is invalid") != std::string::npos ||
            message.find("embedding input must be valid UTF-8 text") != std::string::npos) {
            throw;
        }
        throw std::runtime_error("embedding request failed, cause=" + message);
    }
}

OpenAIEmbeddingClient::OpenAIEmbeddingClient(const std::string &base_url,
                                             const std::string &api_key,
                                             const std::string &model,
                                             int timeout_ms,
                                             int dimensions,
                                             const std::string &organization,
                                             const std::string &project)
    // OpenAI 需要的鉴权头在 transport 层注入，embed 时不必重复设置。
    : transport_(
        base_url,
        timeout_ms,
        {
            {"Authorization", "Bearer " + api_key},
            {"OpenAI-Organization", organization},
            {"OpenAI-Project", project},
        }),
      model_(model),
      dimensions_(dimensions) {
}

std::vector<float> OpenAIEmbeddingClient::embed(const std::string &text) const {
    // OpenAI 同样把空文本视为无效输入，这里直接短路。
    if (text.empty()) {
        return {};
    }

    nlohmann::json req_json;
    req_json["model"] = model_;
    try {
        // OpenAI embeddings 接口接收单字符串 input。
        req_json["input"] = text;
    } catch (const nlohmann::json::exception &) {
        throw std::runtime_error("embedding input must be valid UTF-8 text");
    }
    if (dimensions_ > 0) {
        // dimensions 仅在用户显式配置时才下发。
        req_json["dimensions"] = dimensions_;
    }

    try {
        const HttpResponse response = transport_.post_json("/v1/embeddings", req_json);
        if (response.status_code != 200) {
            throw std::runtime_error("embedding request failed, status=" +
                                     std::to_string(response.status_code) +
                                     ", body=" + response.body);
        }
        // OpenAI 返回 data[0].embedding，这里统一交给兼容解析器处理。
        return parse_embedding_vector(nlohmann::json::parse(response.body));
    } catch (const std::runtime_error &ex) {
        // 对 transport、JSON 解析等底层异常做统一错误归一化。
        const std::string message = ex.what();
        if (message.find("embedding request failed") != std::string::npos ||
            message.find("embedding response format is invalid") != std::string::npos ||
            message.find("embedding input must be valid UTF-8 text") != std::string::npos) {
            throw;
        }
        throw std::runtime_error("embedding request failed, cause=" + message);
    }
}
