//
// Created by hyp on 2026/3/26.
//

#include "embedding_client.h"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

EmbeddingClient::EmbeddingClient(const std::string &host,
                                 const std::string &port,
                                 const std::string &model,
                                 int timeout_ms)
    : host_(host),
      port_(port),
      model_(model),
      timeout_ms_(timeout_ms) {
}

std::vector<float> EmbeddingClient::embed(const std::string &text) const {
    if (text.empty()) {
        return {};
    }
    // 1. 构建请求 JSON
    nlohmann::json req_json;
    req_json["model"] = model_;
    // Ollama /api/embed 支持 input 数组
    try {
        req_json["input"] = nlohmann::json::array({text});
    } catch (const nlohmann::json::exception &) {
        throw std::runtime_error("embedding input must be valid UTF-8 text");
    }
    const std::string body = req_json.dump();
    // 2. 建立 TCP 连接
    asio::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    auto const results = resolver.resolve(host_, port_);
    stream.expires_after(std::chrono::milliseconds(timeout_ms_));
    stream.connect(results);
    // 3. 发送 HTTP 请求
    http::request<http::string_body> req{http::verb::post, "/api/embed", 11};
    req.set(http::field::host, host_);
    req.set(http::field::user_agent, "QtRAG-Server");
    req.set(http::field::content_type, "applicaion/json");
    req.body() = body;
    req.prepare_payload();
    http::write(stream, req);
    // 4. 读取 HTTP 响应
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    // 5. 关闭连接
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_send, ec);
    // 6. 判断 HTTP 状态码
    if (res.result() != http::status::ok) {
        throw std::runtime_error("embedding request failed, status=" +
                                 std::to_string(static_cast<unsigned>(res.result_int())) +
                                 ", body=" + res.body());
    }
    // 7. 解析 JSON
    auto j = nlohmann::json::parse(res.body());
    std::vector<float> result;
    // 兼容 Ollama 新接口：embeddings: [[...]]
    if (j.contains("embeddings") && j["embeddings"].is_array() && !j["embeddings"].empty()) {
        for (const auto &v: j["embeddings"][0]) {
            result.push_back(v.get<float>());
        }
        return result;
    }
    // 兼容一些旧格式：embedding: [...]
    if (j.contains("embedding") && j["embedding"].is_array()) {
        for (const auto &v: j["embedding"]) {
            result.push_back(v.get<float>());
        }
        return result;
    }
    throw std::runtime_error("embedding response format is invalid");
}
