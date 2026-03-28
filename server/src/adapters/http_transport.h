//
// Created by hyp on 2026/3/28.
//

#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

// 统一的额外请求头结构，便于 provider 按需附加鉴权信息。
struct HttpHeader {
    // Header 名，例如 Authorization。
    std::string name;
    // Header 值，例如 Bearer xxx。
    std::string value;
};

struct HttpResponse {
    // 上游返回的 HTTP 状态码。
    unsigned status_code{0};
    // 原始响应体；SSE 场景下这里保存完整事件流文本，便于错误回溯。
    std::string body;
    // 响应头统一转成小写 key，便于后续按名字直接读取。
    std::map<std::string, std::string> headers;
};

// HttpTransport：负责同步发起 JSON 请求，内部同时支持 HTTP / HTTPS。
class HttpTransport {
public:
    // base_url 是完整上游地址，例如 http://127.0.0.1:11434 或 https://api.openai.com。
    // timeout_ms 控制单次请求的整体超时时间。
    // default_headers 会附加到这个 transport 发出的每一个请求上。
    HttpTransport(const std::string &base_url,
                  int timeout_ms,
                  std::vector<HttpHeader> default_headers = {});

    // 发送普通 JSON POST 请求，并一次性读取完整响应。
    HttpResponse post_json(const std::string &path,
                           const nlohmann::json &payload,
                           const std::vector<HttpHeader> &extra_headers = {}) const;

    // 发送 JSON 请求并按 SSE 事件流持续回调上游事件。
    HttpResponse post_json_sse(
        const std::string &path,
        const nlohmann::json &payload,
        const std::function<void(const std::string &event_name, const std::string &data)> &on_event,
        const std::vector<HttpHeader> &extra_headers = {}) const;

    // 发送 JSON 请求并按“单行文本流”持续回调，适合 Ollama 这类 NDJSON 返回。
    HttpResponse post_json_lines(
        const std::string &path,
        const nlohmann::json &payload,
        const std::function<void(const std::string &line)> &on_line,
        const std::vector<HttpHeader> &extra_headers = {}) const;

private:
    // 解析后的基础地址信息，避免每次请求都重新拆 URL。
    struct ParsedBaseUrl {
        // http / https。
        std::string scheme;
        // 主机名，不包含协议和路径。
        std::string host;
        // 端口；如果 URL 未显式提供，会按协议推断默认值。
        std::string port;
        // 允许 base_url 自带路径前缀，例如 https://host/proxy。
        std::string base_path;
        // true 表示后续请求需要做 TLS 握手。
        bool use_tls{false};
    };

    // 启动时解析并校验 base_url，后续请求直接复用结果。
    static ParsedBaseUrl parse_base_url(const std::string &base_url);

    // 把调用方传入的 path 拼接到 base_url 的 path 前缀上。
    std::string build_target(const std::string &path) const;

private:
    // 预解析后的上游地址信息。
    ParsedBaseUrl base_;
    // 单次请求超时。
    int timeout_ms_{30000};
    // 所有请求默认附带的 header。
    std::vector<HttpHeader> default_headers_;
};
