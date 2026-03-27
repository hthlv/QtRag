//
// Created by hyp on 2026/3/26.
//

#include "llm_client.h"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <thread>
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
namespace {
    // 截断文本，避免回答太长
    std::string truncate_text(const std::string &text, std::size_t max_len) {
        if (text.size() <= max_len) {
            return text;
        }
        return text.substr(0, max_len) + "...";
    }

    // 根据 UTF-8 首字节判断当前字符总共占多少字节。
    std::size_t utf8_char_length(unsigned char lead) {
        if ((lead & 0x80) == 0x00) {
            return 1;
        }
        if ((lead & 0xE0) == 0xC0) {
            return 2;
        }
        if ((lead & 0xF0) == 0xE0) {
            return 3;
        }
        if ((lead & 0xF8) == 0xF0) {
            return 4;
        }
        return 1;
    }

    // 按“字符数”而不是“字节数”切分 UTF-8 文本，避免中文被截断后出现乱码。
    std::vector<std::string> split_utf8_by_chars(const std::string &text, std::size_t max_chars) {
        std::vector<std::string> pieces;
        if (max_chars == 0 || text.empty()) {
            return pieces;
        }

        std::size_t start = 0;
        std::size_t char_count = 0;
        std::size_t i = 0;
        while (i < text.size()) {
            const std::size_t char_len = utf8_char_length(static_cast<unsigned char>(text[i]));
            if (char_count == max_chars) {
                // 已累计满一个分片时，从上一个合法边界切出这一段。
                pieces.push_back(text.substr(start, i - start));
                start = i;
                char_count = 0;
            }
            i += std::min(char_len, text.size() - i);
            ++char_count;
        }

        if (start < text.size()) {
            pieces.push_back(text.substr(start));
        }
        return pieces;
    }
}


LLMClient::LLMClient(const std::string &host,
                     const std::string &port,
                     const std::string &model,
                     int timeout_ms)
    : host_(host),
      port_(port),
      model_(model),
      timeout_ms_(timeout_ms) {
}

std::string LLMClient::generate(const std::string &query,
                                const std::vector<RetrievedChunk> &contexts,
                                const std::string &prompt) const {
    (void)query;
    (void)contexts;
    // 1. 构造请求 JSON
    nlohmann::json req_json;
    req_json["model"] = model_;
    req_json["prompt"] = prompt;
    // 这里明确要求非流式，让服务端先拿到完整回答
    req_json["stream"] = false;
    const std::string body = req_json.dump();
    // 2. 建立 TCP 连接
    asio::io_context ioc;
    tcp::resolver resolver(ioc);
    beast::tcp_stream stream(ioc);
    auto const results = resolver.resolve(host_, port_);
    stream.expires_after(std::chrono::milliseconds(timeout_ms_));
    stream.connect(results);
    // 3. 发送请求
    http::request<http::string_body> req{http::verb::post, "/api/generate", 11};
    req.set(http::field::host, host_);
    req.set(http::field::user_agent, "QtRAG-Server");
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();
    http::write(stream, req);
    // 4. 读取响应
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    http::read(stream, buffer, res);
    // 5. 关闭连接
    beast::error_code ec;
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    // 6. 状态码校验
    if (res.result() != http::status::ok) {
        throw std::runtime_error("llm request failed, status=" +
                                 std::to_string(static_cast<unsigned>(res.result_int())) +
                                 ", body=" + res.body());
    }
    // 7. 解析 JSON
    auto j = nlohmann::json::parse(res.body());
    // Ollama /api/generate 非流式返回里，回答文本在 response 字段
    if (!j.contains("response")) {
        throw std::runtime_error("llm response format is invalid");
    }
    return j["response"].get<std::string>();
}

void LLMClient::stream_generate(const std::string &query,
                                const std::vector<RetrievedChunk> &contexts,
                                const std::string &prompt,
                                const std::function<void(const std::string &)> &on_chunk) const {
    // 1. 先生成完整回答
    // 2. 再按字符数分段，避免把 UTF-8 中文切坏
    const std::string full_answer = generate(query, contexts, prompt);
    const std::size_t piece_size = 20;
    // 这里先预切片，后续逐段回调给 SSE 层发送。
    const auto pieces = split_utf8_by_chars(full_answer, piece_size);
    for (const auto &piece: pieces) {
        // 通过回调把当前片段交给上层
        on_chunk(piece);
        // 人工 sleep 一下，模拟流式输出效果
        // 注意：当前服务器还是单线程同步版，这会阻塞其它请求
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}
