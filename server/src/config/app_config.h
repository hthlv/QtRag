//
// Created by hyp on 2026/3/27.
//

#pragma once
#include <string>

struct AppConfig {
    std::string listen_address = "127.0.0.1";
    unsigned short listen_port = 8080;
    std::size_t worker_threads = 4;
    // 上游模型服务（Ollama）
    std::string provider_host = "127.0.0.1";
    std::string provider_port = "11434";
    int provider_timeout_ms = 30000;
    // 模型名
    std::string chat_model = "qwen2.5:7b";
    std::string embedding_model = "nomic-embed-text";
    // 从 JSON 文件加载配置
    static AppConfig load_from_file(const std::string &path);
};
