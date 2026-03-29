//
// Created by hyp on 2026/3/27.
//

#include "app_config.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {
    // 读取对象配置段；不存在或类型不对时返回空对象，便于兼容旧平铺配置。
    nlohmann::json read_object_or_empty(const nlohmann::json &j, const char *key) {
        if (!j.contains(key) || !j.at(key).is_object()) {
            return nlohmann::json::object();
        }
        return j.at(key);
    }

    // 允许配置里把数字写成字符串，减少手工改配置时的脆弱性。
    int read_int_or_default(const nlohmann::json &j, const char *key, int default_value) {
        if (!j.contains(key) || j.at(key).is_null()) {
            return default_value;
        }
        const auto &value = j.at(key);
        if (value.is_number_integer()) {
            return value.get<int>();
        }
        if (value.is_number_unsigned()) {
            return static_cast<int>(value.get<unsigned int>());
        }
        if (value.is_string()) {
            return std::stoi(value.get<std::string>());
        }
        return default_value;
    }

    // 有些配置可能被写成数字/布尔，统一兜底转成字符串读取。
    std::string read_string_or_default(const nlohmann::json &j,
                                       const char *key,
                                       const std::string &default_value) {
        if (!j.contains(key) || j.at(key).is_null()) {
            return default_value;
        }
        const auto &value = j.at(key);
        if (value.is_string()) {
            return value.get<std::string>();
        }
        return value.dump();
    }

    // 布尔配置兼容 true/false、1/0 和 yes/no。
    bool read_bool_or_default(const nlohmann::json &j, const char *key, bool default_value) {
        if (!j.contains(key) || j.at(key).is_null()) {
            return default_value;
        }
        const auto &value = j.at(key);
        if (value.is_boolean()) {
            return value.get<bool>();
        }
        if (value.is_number_integer()) {
            return value.get<int>() != 0;
        }
        if (value.is_string()) {
            std::string lowered = value.get<std::string>();
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return lowered == "true" || lowered == "1" || lowered == "yes";
        }
        return default_value;
    }

    // 新结构优先读嵌套对象，找不到时再回退到旧的平铺字段。
    int read_int_with_legacy_fallback(const nlohmann::json &nested,
                                      const char *nested_key,
                                      const nlohmann::json &root,
                                      const char *legacy_key,
                                      int default_value) {
        if (nested.contains(nested_key)) {
            return read_int_or_default(nested, nested_key, default_value);
        }
        return read_int_or_default(root, legacy_key, default_value);
    }

    // 字符串配置同样优先读新结构，再回退旧结构。
    std::string read_string_with_legacy_fallback(const nlohmann::json &nested,
                                                 const char *nested_key,
                                                 const nlohmann::json &root,
                                                 const char *legacy_key,
                                                 const std::string &default_value) {
        if (nested.contains(nested_key)) {
            return read_string_or_default(nested, nested_key, default_value);
        }
        return read_string_or_default(root, legacy_key, default_value);
    }

    // 布尔配置优先读新结构，再回退旧结构。
    bool read_bool_with_legacy_fallback(const nlohmann::json &nested,
                                        const char *nested_key,
                                        const nlohmann::json &root,
                                        const char *legacy_key,
                                        bool default_value) {
        if (nested.contains(nested_key)) {
            return read_bool_or_default(nested, nested_key, default_value);
        }
        return read_bool_or_default(root, legacy_key, default_value);
    }

    std::vector<LlmOptionConfig> read_llm_options(const nlohmann::json &root,
                                                  const ProviderConfig &default_provider,
                                                  const OpenAIConfig &default_openai,
                                                  std::string *default_llm_id) {
        std::vector<LlmOptionConfig> options;
        if (!root.contains("llm_options") || !root.at("llm_options").is_array()) {
            return options;
        }

        const auto &items = root.at("llm_options");
        options.reserve(items.size());
        bool has_explicit_default = false;
        for (std::size_t i = 0; i < items.size(); ++i) {
            // 跳过非法项，避免因为单条配置错误导致整体启动失败。
            if (!items.at(i).is_object()) {
                continue;
            }
            const auto &item = items.at(i);
            const nlohmann::json provider_json = read_object_or_empty(item, "provider");
            const nlohmann::json openai_json = read_object_or_empty(item, "openai");

            LlmOptionConfig option;
            option.id = read_string_or_default(item, "id", "llm_" + std::to_string(i));
            option.label = read_string_or_default(item, "label", option.id);
            option.model = read_string_or_default(item, "model", "");
            option.is_default = read_bool_or_default(item, "default", false);
            if (option.model.empty()) {
                continue;
            }

            option.provider = default_provider;
            option.provider.type = read_string_or_default(provider_json, "type", default_provider.type);
            option.provider.base_url = read_string_or_default(provider_json, "base_url", default_provider.base_url);
            option.provider.timeout_ms = read_int_or_default(provider_json, "timeout_ms", default_provider.timeout_ms);

            // OpenAI 私有参数允许在每个模型上单独覆盖。
            option.openai = default_openai;
            option.openai.api_key_env = read_string_or_default(openai_json, "api_key_env", default_openai.api_key_env);
            option.openai.chat_api = read_string_or_default(openai_json, "chat_api", default_openai.chat_api);
            option.openai.organization = read_string_or_default(openai_json, "organization", default_openai.organization);
            option.openai.project = read_string_or_default(openai_json, "project", default_openai.project);
            option.openai.store = read_bool_or_default(openai_json, "store", default_openai.store);
            option.openai.reasoning_effort = read_string_or_default(
                openai_json, "reasoning_effort", default_openai.reasoning_effort);
            option.openai.embedding_dimensions = read_int_or_default(
                openai_json, "embedding_dimensions", default_openai.embedding_dimensions);

            if (option.is_default && !has_explicit_default) {
                *default_llm_id = option.id;
                has_explicit_default = true;
            }
            options.push_back(std::move(option));
        }

        if (!has_explicit_default) {
            // 没有显式 default 时，按列表第一项作为默认模型。
            options.front().is_default = true;
            *default_llm_id = options.front().id;
        }
        return options;
    }
}

AppConfig AppConfig::load_from_file(const std::string &path) {
    // 先用代码默认值初始化，配置文件只覆盖显式提供的项。
    AppConfig cfg;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        // 如果配置文件不存在，就直接返回默认配置
        return cfg;
    }
    nlohmann::json j;
    // 当前配置文件规模很小，直接一次性反序列化即可。
    ifs >> j;
    const nlohmann::json server_json = read_object_or_empty(j, "server");
    const nlohmann::json provider_json = read_object_or_empty(j, "provider");
    const nlohmann::json models_json = read_object_or_empty(j, "models");
    const nlohmann::json openai_json = read_object_or_empty(j, "openai");

    // 先读服务本地配置。
    cfg.server.listen_address = read_string_with_legacy_fallback(
        server_json, "listen_address", j, "listen_address", cfg.server.listen_address);
    cfg.server.listen_port = static_cast<unsigned short>(read_int_with_legacy_fallback(
        server_json, "listen_port", j, "listen_port", static_cast<int>(cfg.server.listen_port)));
    cfg.server.worker_threads = static_cast<std::size_t>(read_int_with_legacy_fallback(
        server_json, "worker_threads", j, "worker_threads", static_cast<int>(cfg.server.worker_threads)));

    // 再读 provider 公共配置。
    cfg.provider.type = read_string_with_legacy_fallback(
        provider_json, "type", j, "provider_type", cfg.provider.type);
    cfg.provider.timeout_ms = read_int_with_legacy_fallback(
        provider_json, "timeout_ms", j, "provider_timeout_ms", cfg.provider.timeout_ms);

    // base_url 优先读新结构，其次回退旧平铺字段。
    cfg.provider.base_url = read_string_with_legacy_fallback(
        provider_json, "base_url", j, "provider_base_url", "");
    if (cfg.provider.base_url.empty()) {
        const std::string legacy_provider_host = read_string_or_default(j, "provider_host", "127.0.0.1");
        const std::string legacy_provider_port = read_string_or_default(j, "provider_port", "11434");
        // 兼容旧配置：如果没写 base_url，就回退到 host + port 组合。
        cfg.provider.base_url = "http://" + legacy_provider_host + ":" + legacy_provider_port;
    }

    // embedding 模型继续保留在公共模型层。
    cfg.models.embedding = read_string_with_legacy_fallback(
        models_json, "embedding", j, "embedding_model", cfg.models.embedding);

    // 最后读 OpenAI 私有配置。
    cfg.openai.api_key_env = read_string_with_legacy_fallback(
        openai_json, "api_key_env", j, "openai_api_key_env", cfg.openai.api_key_env);
    cfg.openai.chat_api = read_string_with_legacy_fallback(
        openai_json, "chat_api", j, "openai_chat_api", cfg.openai.chat_api);
    cfg.openai.organization = read_string_with_legacy_fallback(
        openai_json, "organization", j, "openai_organization", cfg.openai.organization);
    cfg.openai.project = read_string_with_legacy_fallback(
        openai_json, "project", j, "openai_project", cfg.openai.project);
    cfg.openai.store = read_bool_with_legacy_fallback(
        openai_json, "store", j, "openai_store", cfg.openai.store);
    cfg.openai.reasoning_effort = read_string_with_legacy_fallback(
        openai_json, "reasoning_effort", j, "openai_reasoning_effort", cfg.openai.reasoning_effort);
    // 0 表示不显式指定维度，交给上游模型默认值处理。
    cfg.openai.embedding_dimensions = read_int_with_legacy_fallback(
        openai_json, "embedding_dimensions", j, "openai_embedding_dimensions", cfg.openai.embedding_dimensions);
    cfg.llm_options = read_llm_options(j, cfg.provider, cfg.openai, &cfg.default_llm_id);
    if (cfg.llm_options.empty()) {
        throw std::runtime_error("llm_options must contain at least one valid chat model");
    }
    return cfg;
}
