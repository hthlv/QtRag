//
// Created by hyp on 2026/3/27.
//

#pragma once
#include <string>

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
    // 对话模型名。
    std::string chat = "qwen2.5:7b";
    // 向量模型名。
    std::string embedding = "nomic-embed-text";
};

// OpenAI provider 私有配置。
struct OpenAIConfig {
    // API Key 只从环境变量读取，不落盘。
    std::string api_key_env = "OPENAI_API_KEY";
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

struct AppConfig {
    // 本地服务配置。
    ServerRuntimeConfig server;
    // 上游 provider 公共配置。
    ProviderConfig provider;
    // 当前所用模型名配置。
    ModelConfig models;
    // OpenAI 私有配置。
    OpenAIConfig openai;
    // 从 JSON 文件加载配置
    static AppConfig load_from_file(const std::string &path);
};
