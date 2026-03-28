//
// Created by hyp on 2026/3/26.
//

#include "llm_client.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
    // OpenAI Responses API 返回的是 output 数组，需要把 message/output_text 全部拼出来。
    std::string extract_openai_output_text(const nlohmann::json &json_body) {
        if (!json_body.contains("output") || !json_body["output"].is_array()) {
            throw std::runtime_error("llm response format is invalid");
        }

        std::string result;
        for (const auto &item : json_body["output"]) {
            if (!item.is_object()) {
                continue;
            }
            if (!item.contains("type") || item.value("type", "") != "message") {
                continue;
            }
            if (!item.contains("content") || !item["content"].is_array()) {
                continue;
            }
            for (const auto &content : item["content"]) {
                if (!content.is_object()) {
                    continue;
                }
                const std::string content_type = content.value("type", "");
                if ((content_type == "output_text" || content_type == "text") &&
                    content.contains("text") &&
                    content["text"].is_string()) {
                    result += content["text"].get<std::string>();
                }
            }
        }

        if (result.empty()) {
            throw std::runtime_error("llm response format is invalid");
        }
        return result;
    }

    // 把 OpenAI 的公共请求体抽出来，避免普通/流式两套逻辑各拼一遍。
    nlohmann::json build_openai_request_json(const std::string &model,
                                             const std::string &prompt,
                                             bool store,
                                             const std::string &reasoning_effort,
                                             bool stream) {
        nlohmann::json req_json;
        req_json["model"] = model;
        req_json["input"] = prompt;
        req_json["store"] = store;
        req_json["stream"] = stream;
        if (!reasoning_effort.empty()) {
            req_json["reasoning"] = {
                {"effort", reasoning_effort}
            };
        }
        return req_json;
    }

    // OpenAI 的错误体可能出现在 error.message，也可能直接是 message。
    std::string extract_openai_error_message(const nlohmann::json &json_body) {
        if (json_body.contains("error") && json_body["error"].is_object()) {
            const auto &error = json_body["error"];
            if (error.contains("message") && error["message"].is_string()) {
                return error["message"].get<std::string>();
            }
        }
        if (json_body.contains("message") && json_body["message"].is_string()) {
            return json_body["message"].get<std::string>();
        }
        return json_body.dump();
    }

    // 流式事件里真正的增量文本一般在 delta；个别兼容场景退回读 text。
    std::string extract_openai_delta_text(const nlohmann::json &json_body) {
        if (json_body.contains("delta") && json_body["delta"].is_string()) {
            return json_body["delta"].get<std::string>();
        }
        if (json_body.contains("text") && json_body["text"].is_string()) {
            return json_body["text"].get<std::string>();
        }
        return {};
    }

    // Ollama 流式返回每行一个 JSON 对象，文本增量放在 response 字段。
    std::string extract_ollama_delta_text(const nlohmann::json &json_body) {
        if (json_body.contains("response") && json_body["response"].is_string()) {
            return json_body["response"].get<std::string>();
        }
        return {};
    }
}

OllamaLLMClient::OllamaLLMClient(const std::string &base_url,
                                 const std::string &model,
                                 int timeout_ms)
    // 复用统一 transport，Ollama 实现只关心自己的请求/响应格式。
    : transport_(base_url, timeout_ms),
      model_(model) {
}

std::string OllamaLLMClient::generate(const std::string &query,
                                      const std::vector<RetrievedChunk> &contexts,
                                      const std::string &prompt) const {
    // 当前 prompt 已经包含完整上下文，因此这里不再单独拼 query/contexts。
    (void)query;
    (void)contexts;

    nlohmann::json req_json;
    req_json["model"] = model_;
    req_json["prompt"] = prompt;
    req_json["stream"] = false;

    try {
        // Ollama 非流式仍走 /api/generate。
        const HttpResponse response = transport_.post_json("/api/generate", req_json);
        if (response.status_code != 200) {
            throw std::runtime_error("llm request failed, status=" +
                                     std::to_string(response.status_code) +
                                     ", body=" + response.body);
        }
        const auto json_body = nlohmann::json::parse(response.body);
        if (!json_body.contains("response") || !json_body["response"].is_string()) {
            throw std::runtime_error("llm response format is invalid");
        }
        return json_body["response"].get<std::string>();
    } catch (const std::runtime_error &ex) {
        // 统一把底层 transport/JSON 异常包装成 llm request failed 前缀。
        const std::string message = ex.what();
        if (message.find("llm request failed") != std::string::npos ||
            message.find("llm response format is invalid") != std::string::npos) {
            throw;
        }
        throw std::runtime_error("llm request failed, cause=" + message);
    }
}

void OllamaLLMClient::stream_generate(const std::string &query,
                                      const std::vector<RetrievedChunk> &contexts,
                                      const std::string &prompt,
                                      const std::function<void(const std::string &)> &on_chunk) const {
    // Ollama 流式同样直接消费最终 prompt。
    (void)query;
    (void)contexts;

    nlohmann::json req_json;
    req_json["model"] = model_;
    req_json["prompt"] = prompt;
    req_json["stream"] = true;

    try {
        const HttpResponse response = transport_.post_json_lines(
            "/api/generate",
            req_json,
            [&](const std::string &line) {
                nlohmann::json event_json;
                try {
                    event_json = nlohmann::json::parse(line);
                } catch (const nlohmann::json::exception &) {
                    throw std::runtime_error("llm response format is invalid");
                }

                const std::string delta = extract_ollama_delta_text(event_json);
                if (!delta.empty()) {
                    on_chunk(delta);
                }
            });

        if (response.status_code != 200) {
            throw std::runtime_error("llm request failed, status=" +
                                     std::to_string(response.status_code) +
                                     ", body=" + response.body);
        }
    } catch (const std::runtime_error &ex) {
        const std::string message = ex.what();
        if (message.find("llm request failed") != std::string::npos ||
            message.find("llm response format is invalid") != std::string::npos) {
            throw;
        }
        throw std::runtime_error("llm request failed, cause=" + message);
    }
}

OpenAILLMClient::OpenAILLMClient(const std::string &base_url,
                                 const std::string &api_key,
                                 const std::string &model,
                                 int timeout_ms,
                                 bool store,
                                 const std::string &reasoning_effort,
                                 const std::string &organization,
                                 const std::string &project)
    // OpenAI 所需鉴权与租户头信息在构造 transport 时一次性注入。
    : transport_(
        base_url,
        timeout_ms,
        {
            {"Authorization", "Bearer " + api_key},
            {"OpenAI-Organization", organization},
            {"OpenAI-Project", project},
        }),
      model_(model),
      store_(store),
      reasoning_effort_(reasoning_effort) {
}

std::string OpenAILLMClient::generate(const std::string &query,
                                      const std::vector<RetrievedChunk> &contexts,
                                      const std::string &prompt) const {
    // OpenAI 版本同样直接消费最终 prompt。
    (void)query;
    (void)contexts;
    const nlohmann::json req_json = build_openai_request_json(
        model_,
        prompt,
        store_,
        reasoning_effort_,
        false);

    try {
        const HttpResponse response = transport_.post_json("/v1/responses", req_json);
        if (response.status_code != 200) {
            throw std::runtime_error("llm request failed, status=" +
                                     std::to_string(response.status_code) +
                                     ", body=" + response.body);
        }
        // Responses API 需要从 output/message/content 中提取最终文本。
        return extract_openai_output_text(nlohmann::json::parse(response.body));
    } catch (const std::runtime_error &ex) {
        const std::string message = ex.what();
        if (message.find("llm request failed") != std::string::npos ||
            message.find("llm response format is invalid") != std::string::npos) {
            throw;
        }
        throw std::runtime_error("llm request failed, cause=" + message);
    }
}

void OpenAILLMClient::stream_generate(const std::string &query,
                                      const std::vector<RetrievedChunk> &contexts,
                                      const std::string &prompt,
                                      const std::function<void(const std::string &)> &on_chunk) const {
    // OpenAI 流式模式直接消费上游 SSE，而不是本地伪造 token。
    (void)query;
    (void)contexts;
    const nlohmann::json req_json = build_openai_request_json(
        model_,
        prompt,
        store_,
        reasoning_effort_,
        true);

    try {
        const HttpResponse response = transport_.post_json_sse(
            "/v1/responses",
            req_json,
            [&](const std::string &event_name, const std::string &data) {
                // [DONE] 或空数据都不再向上转发。
                if (data.empty() || data == "[DONE]") {
                    return;
                }

                nlohmann::json event_json;
                try {
                    // 每条 SSE data 都应该是独立 JSON。
                    event_json = nlohmann::json::parse(data);
                } catch (const nlohmann::json::exception &) {
                    throw std::runtime_error("llm response format is invalid");
                }

                // 某些服务端会把类型写在 event 字段里，也可能只放在 JSON.type 中。
                const std::string resolved_event_name = !event_name.empty()
                                                            ? event_name
                                                            : event_json.value("type", "");

                if (resolved_event_name == "response.output_text.delta") {
                    // 只把真正的文本增量透传给业务层，保持现有 token 协议不变。
                    const std::string delta = extract_openai_delta_text(event_json);
                    if (!delta.empty()) {
                        on_chunk(delta);
                    }
                    return;
                }

                if (resolved_event_name == "error") {
                    // OpenAI 的 error 事件直接转成统一的 llm 请求异常。
                    throw std::runtime_error("llm request failed, body=" + extract_openai_error_message(event_json));
                }
            });

        if (response.status_code != 200) {
            // 如果上游在响应头阶段就失败，这里仍然把完整 body 抛出去。
            throw std::runtime_error("llm request failed, status=" +
                                     std::to_string(response.status_code) +
                                     ", body=" + response.body);
        }
    } catch (const std::runtime_error &ex) {
        const std::string message = ex.what();
        if (message.find("llm request failed") != std::string::npos ||
            message.find("llm response format is invalid") != std::string::npos) {
            throw;
        }
        throw std::runtime_error("llm request failed, cause=" + message);
    }
}
