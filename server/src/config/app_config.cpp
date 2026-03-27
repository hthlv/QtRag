//
// Created by hyp on 2026/3/27.
//

#include "app_config.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

AppConfig AppConfig::load_from_file(const std::string &path) {
    AppConfig cfg;
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        // 如果配置文件不存在，就直接返回默认配置
        return cfg;
    }
    nlohmann::json j;
    ifs >> j;
    // 逐项读取，缺失时使用默认值
    cfg.listen_address = j.value("listen_address", cfg.listen_address);
    cfg.listen_port = static_cast<unsigned short>(j.value("listen_port", static_cast<int>(cfg.listen_port)));
    cfg.worker_threads = j.value("worker_threads", cfg.worker_threads);
    cfg.provider_host = j.value("provider_host", cfg.provider_host);
    cfg.provider_port = j.value("provider_port", cfg.provider_port);
    cfg.provider_timeout_ms = j.value("provider_timeout_ms", cfg.provider_timeout_ms);
    cfg.chat_model = j.value("chat_model", cfg.chat_model);
    cfg.embedding_model = j.value("embedding_model", cfg.embedding_model);
    return cfg;
}
