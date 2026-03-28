//
// Created by hyp on 2026/3/28.
//

#pragma once
#include <memory>

#include "config/app_config.h"

class EmbeddingClient;
class LLMClient;

// 按配置创建对应的上游 provider，避免业务层感知 OpenAI / Ollama 细节。
std::unique_ptr<EmbeddingClient> create_embedding_client(const AppConfig &config);

std::unique_ptr<LLMClient> create_llm_client(const AppConfig &config);
