//
// Created by hyp on 2026/3/27.
//

#pragma once
#include <string>
#include <vector>

// 服务端本地运行参数。
struct ServerRuntimeConfig {
    // 服务端本地监听地址。
    std::string listen_address = "127.0.0.1";
    // 服务端监听端口。
    unsigned short listen_port = 8080;
    // 处理请求的 worker 线程数。
    std::size_t worker_threads = 4;
};

// 上游 provider 的通用传输配置。
struct ProviderConfig {
    // 上游 provider 类型：当前支持 ollama / openai。
    std::string type = "ollama";
    // 通用上游地址，支持 http / https。
    std::string base_url = "http://127.0.0.1:11434";
    // 所有上游请求共享的超时时间，单位毫秒。
    int timeout_ms = 30000;
};

// 不同 provider 共用的模型名配置。
struct ModelConfig {
    // 仅保留 embedding 模型名；聊天模型统一走 llm_options。
    std::string embedding = "nomic-embed-text";
};

// OpenAI provider 私有配置。
struct OpenAIConfig {
    // API Key 只从环境变量读取，不落盘。
    std::string api_key_env = "OPENAI_API_KEY";
    // 聊天接口风格：responses / chat_completions。
    std::string chat_api = "responses";
    // 可选组织头。
    std::string organization;
    // 可选项目头。
    std::string project;
    // 是否允许 OpenAI 存储响应，默认关闭。
    bool store = false;
    // reasoning effort 为可选参数。
    std::string reasoning_effort;
    // 0 表示不显式指定 embedding 维度。
    int embedding_dimensions = 0;
};

// 单个可选聊天模型的服务端配置。
struct LlmOptionConfig {
    // 客户端提交和服务端内部路由使用的稳定 ID。
    std::string id = "default";
    // 展示给客户端的模型名称。
    std::string label = "default";
    // 该模型对应的 provider 配置。
    ProviderConfig provider;
    // 该聊天模型实际使用的上游模型名。
    std::string model = "qwen2.5:7b";
    // 该模型是否为服务端默认模型。
    bool is_default = false;
    // 仅 OpenAI 兼容 provider 使用的私有配置。
    OpenAIConfig openai;
};

struct AppConfig {
    // 本地服务配置。
    ServerRuntimeConfig server;
    // 上游 provider 公共配置。
    ProviderConfig provider;
    // 当前所用模型名配置。
    ModelConfig models;
    // OpenAI 私有配置。
    OpenAIConfig openai;
    // 可选聊天模型列表；聊天模型只从这里读取，启动时至少需要一个有效项。
    std::vector<LlmOptionConfig> llm_options;
    // 当前默认聊天模型 ID。
    std::string default_llm_id = "default";
    // 从 JSON 文件加载配置
    static AppConfig load_from_file(const std::string &path);
};
