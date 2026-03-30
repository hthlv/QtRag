//
// Created by hyp on 2026/3/28.
//

#include "http_transport.h"
#include "utils/logger.h"

#include <boost/asio.hpp>
#ifdef QTRAG_SERVER_HAS_OPENSSL
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>
#else
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <limits>
#include <sstream>
#include <string_view>
#include <stdexcept>

namespace asio = boost::asio;
#ifdef QTRAG_SERVER_HAS_OPENSSL
namespace ssl = asio::ssl;
#endif
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {
    // 统一把 header 名转成小写，便于后续不区分大小写读取。
    std::string to_lower_copy(const std::string &text) {
        std::string lowered = text;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return lowered;
    }

    // 从统一转小写后的 header map 中读取指定 header。
    std::string get_header_value(const std::map<std::string, std::string> &headers, const std::string &name) {
        const auto it = headers.find(to_lower_copy(name));
        return it == headers.end() ? std::string() : it->second;
    }

    // 错误日志里只保留有限长度的 body，避免把整段 HTML 或长 JSON 写满日志。
    std::string truncate_for_log(const std::string &text, std::size_t max_len = 1500) {
        if (text.size() <= max_len) {
            return text;
        }
        return text.substr(0, max_len) + "...";
    }

    // 把请求级别的 OpenAI/SiliconFlow 关键头部拼成统一的日志片段。
    std::string build_openai_header_summary(const std::map<std::string, std::string> &headers) {
        std::ostringstream oss;

        const std::string request_id = get_header_value(headers, "x-request-id");
        if (!request_id.empty()) {
            oss << " request_id=" << request_id;
        }

        const std::string silicon_trace_id = get_header_value(headers, "x-siliconcloud-trace-id");
        if (!silicon_trace_id.empty()) {
            oss << " trace_id=" << silicon_trace_id;
        }

        const std::string processing_ms = get_header_value(headers, "openai-processing-ms");
        if (!processing_ms.empty()) {
            oss << " processing_ms=" << processing_ms;
        }

        const std::string limit_requests = get_header_value(headers, "x-ratelimit-limit-requests");
        const std::string remaining_requests = get_header_value(headers, "x-ratelimit-remaining-requests");
        const std::string reset_requests = get_header_value(headers, "x-ratelimit-reset-requests");
        if (!limit_requests.empty() || !remaining_requests.empty() || !reset_requests.empty()) {
            oss << " req_limit=";
            oss << (remaining_requests.empty() ? "?" : remaining_requests);
            oss << "/";
            oss << (limit_requests.empty() ? "?" : limit_requests);
            if (!reset_requests.empty()) {
                oss << " req_reset=" << reset_requests;
            }
        }

        const std::string limit_tokens = get_header_value(headers, "x-ratelimit-limit-tokens");
        const std::string remaining_tokens = get_header_value(headers, "x-ratelimit-remaining-tokens");
        const std::string reset_tokens = get_header_value(headers, "x-ratelimit-reset-tokens");
        if (!limit_tokens.empty() || !remaining_tokens.empty() || !reset_tokens.empty()) {
            oss << " tok_limit=";
            oss << (remaining_tokens.empty() ? "?" : remaining_tokens);
            oss << "/";
            oss << (limit_tokens.empty() ? "?" : limit_tokens);
            if (!reset_tokens.empty()) {
                oss << " tok_reset=" << reset_tokens;
            }
        }

        return oss.str();
    }

    // 上游请求的统一日志出口：成功和失败都经过这里，避免各 provider 自己重复记录。
    void log_upstream_response(const std::string &method,
                               const std::string &target,
                               const HttpResponse &response,
                               long long elapsed_ms,
                               bool is_streaming) {
        std::ostringstream oss;
        oss << method << " " << target
                << " status=" << response.status_code
                << " elapsed_ms=" << elapsed_ms
                << (is_streaming ? " mode=sse" : " mode=json")
                << build_openai_header_summary(response.headers);

        if (response.status_code >= 400) {
            if (!response.body.empty()) {
                oss << " upstream_error_body=" << truncate_for_log(response.body);
            }
            log_error("upstream", oss.str());
            return;
        }

        log_info("upstream", oss.str());
    }

    // transport 内部异常统一带上 method/path，方便直接定位是哪一次上游请求失败。
    [[noreturn]] void throw_transport_error(const std::string &method,
                                            const std::string &target,
                                            const std::exception &ex) {
        std::ostringstream oss;
        oss << method << " " << target << " failed before response: " << ex.what();
        log_error("upstream", oss.str());
        throw std::runtime_error(std::string("http transport failed: ") + ex.what());
    }

    // 把上游 SSE 字节流按 event/data 语义还原成完整事件。
    class SseEventParser {
    public:
        // on_event 由上层提供，用于消费已经组装好的 SSE 事件。
        explicit SseEventParser(
            const std::function<void(const std::string &, const std::string &)> &on_event)
            : on_event_(on_event) {
        }

        // 持续喂入网络层读取到的原始字节块。
        void feed(std::string_view bytes) {
            for (char ch: bytes) {
                if (ch == '\n') {
                    process_line();
                } else {
                    current_line_ += ch;
                }
            }
        }

        // 连接结束时补发最后一个尚未完整刷出的事件。
        void finish() {
            if (!current_line_.empty()) {
                process_line();
            }
            dispatch_event();
        }

    private:
        // SSE 以空行分隔事件，因此这里按“单行”做最小语义解析。
        void process_line() {
            // 某些服务端会带 CRLF，这里先把末尾 \r 去掉。
            if (!current_line_.empty() && current_line_.back() == '\r') {
                current_line_.pop_back();
            }

            if (current_line_.empty()) {
                dispatch_event();
                return;
            }

            if (current_line_.rfind("event:", 0) == 0) {
                // event: xxx
                event_name_ = trim_left(current_line_.substr(6));
            } else if (current_line_.rfind("data:", 0) == 0) {
                // data 允许多行，因此需要用换行拼回去。
                if (!event_data_.empty()) {
                    event_data_ += '\n';
                }
                event_data_ += trim_left(current_line_.substr(5));
            }

            current_line_.clear();
        }

        // 收到空行后把当前事件交给上层消费。
        void dispatch_event() {
            if (event_name_.empty() && event_data_.empty()) {
                return;
            }
            if (on_event_) {
                on_event_(event_name_, event_data_);
            }
            event_name_.clear();
            event_data_.clear();
        }

        // SSE 协议里冒号后的第一个空格没有语义，统一左裁掉。
        static std::string trim_left(const std::string &text) {
            std::size_t pos = 0;
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            return text.substr(pos);
        }

    private:
        std::function<void(const std::string &, const std::string &)> on_event_;
        // 当前正在积累的单行文本。
        std::string current_line_;
        // 当前事件名。
        std::string event_name_;
        // 当前事件 data 内容。
        std::string event_data_;
    };

    // 把按换行分隔的增量文本流还原成完整行，适合 Ollama NDJSON。
    class LineStreamParser {
    public:
        explicit LineStreamParser(const std::function<void(const std::string &)> &on_line)
            : on_line_(on_line) {
        }

        // 持续喂入网络层读取到的原始字节块。
        void feed(std::string_view bytes) {
            for (char ch: bytes) {
                if (ch == '\n') {
                    dispatch_line();
                } else {
                    current_line_ += ch;
                }
            }
        }

        // 连接结束后把最后一行补发出去。
        void finish() {
            if (!current_line_.empty()) {
                dispatch_line();
            }
        }

    private:
        void dispatch_line() {
            if (!current_line_.empty() && current_line_.back() == '\r') {
                current_line_.pop_back();
            }
            if (!current_line_.empty() && on_line_) {
                on_line_(current_line_);
            }
            current_line_.clear();
        }

    private:
        std::function<void(const std::string &)> on_line_;
        std::string current_line_;
    };

    // 兼容 response / response_parser 两种消息对象的 header 收集逻辑。
    template<typename MessageLike>
    std::map<std::string, std::string> collect_headers(const MessageLike &res) {
        std::map<std::string, std::string> headers;
        for (const auto &field: res.base()) {
            headers[to_lower_copy(field.name_string().to_string())] = field.value().to_string();
        }
        return headers;
    }

    // 只有值非空的 header 才真正写入请求，避免把空组织信息发给上游。
    void set_request_headers(http::request<http::string_body> &req,
                             const std::vector<HttpHeader> &headers) {
        for (const auto &header: headers) {
            if (!header.name.empty() && !header.value.empty()) {
                req.set(header.name, header.value);
            }
        }
    }

    // 读取上游 SSE 响应头和增量 body，并把每个事件回调给调用方。
    template<typename SyncStream>
    HttpResponse read_sse_response(
        SyncStream &stream,
        const http::request<http::string_body> &req,
        const std::function<void(const std::string &, const std::string &)> &on_event) {
        // buffer_body 允许我们边收边解析，不需要一次性把整个响应体放进内存。
        beast::flat_buffer buffer;
        http::response_parser<http::buffer_body> parser;
        parser.body_limit((std::numeric_limits<std::uint64_t>::max)());

        // 先发请求，再先读取响应头，拿到状态码和 header。
        http::write(stream, req);
        http::read_header(stream, buffer, parser);

        HttpResponse response;
        response.status_code = static_cast<unsigned>(parser.get().result_int());
        response.headers = collect_headers(parser.get());

        // 只有非 200 错误流才缓存完整 body；成功 SSE 只做增量转发，避免回答越长内存越涨。
        SseEventParser sse_parser(on_event);
        std::string body;
        const bool should_buffer_body = response.status_code != 200;

        try {
            while (!parser.is_done()) {
                // 每轮把一小块 body 读到栈上的临时缓冲区。
                char chunk[8192];
                parser.get().body().data = chunk;
                parser.get().body().size = sizeof(chunk);

                beast::error_code ec;
                http::read(stream, buffer, parser, ec);
                const std::size_t bytes_read = sizeof(chunk) - parser.get().body().size;
                if (bytes_read > 0) {
                    if (should_buffer_body) {
                        body.append(chunk, bytes_read);
                    }
                    if (response.status_code == 200) {
                        sse_parser.feed(std::string_view(chunk, bytes_read));
                    }
                }
                // Beast 在 buffer_body 模式下可能返回 need_buffer，这不是错误。
                if (ec == http::error::need_buffer) {
                    ec = {};
                }
                if (ec) {
                    throw beast::system_error(ec);
                }
            }

            if (response.status_code == 200) {
                sse_parser.finish();
            }
        } catch (const std::exception &ex) {
            // 即使流式回调阶段抛错，也尽量把目前已拿到的头和 body 一起带上。
            response.body = body;
            std::ostringstream oss;
            oss << "POST " << req.target()
                    << " status=" << response.status_code
                    << " mode=sse"
                    << build_openai_header_summary(response.headers);
            if (!response.body.empty()) {
                oss << " upstream_error_body=" << truncate_for_log(response.body);
            }
            oss << " stream_callback_error=" << ex.what();
            log_error("upstream", oss.str());
            throw;
        }
        response.body = body;
        return response;
    }

    // 读取按行返回的上游流式响应，并把每一行增量交给调用方。
    template<typename SyncStream>
    HttpResponse read_line_stream_response(
        SyncStream &stream,
        const http::request<http::string_body> &req,
        const std::function<void(const std::string &)> &on_line) {
        beast::flat_buffer buffer;
        http::response_parser<http::buffer_body> parser;
        parser.body_limit((std::numeric_limits<std::uint64_t>::max)());

        http::write(stream, req);
        http::read_header(stream, buffer, parser);

        HttpResponse response;
        response.status_code = static_cast<unsigned>(parser.get().result_int());
        response.headers = collect_headers(parser.get());

        LineStreamParser line_parser(on_line);
        std::string body;
        const bool should_buffer_body = response.status_code != 200;

        try {
            while (!parser.is_done()) {
                char chunk[8192];
                parser.get().body().data = chunk;
                parser.get().body().size = sizeof(chunk);

                beast::error_code ec;
                http::read(stream, buffer, parser, ec);
                const std::size_t bytes_read = sizeof(chunk) - parser.get().body().size;
                if (bytes_read > 0) {
                    if (should_buffer_body) {
                        body.append(chunk, bytes_read);
                    }
                    if (response.status_code == 200) {
                        line_parser.feed(std::string_view(chunk, bytes_read));
                    }
                }
                if (ec == http::error::need_buffer) {
                    ec = {};
                }
                if (ec) {
                    throw beast::system_error(ec);
                }
            }

            if (response.status_code == 200) {
                line_parser.finish();
            }
        } catch (const std::exception &ex) {
            response.body = body;
            std::ostringstream oss;
            oss << "POST " << req.target()
                    << " status=" << response.status_code
                    << " mode=line-stream"
                    << build_openai_header_summary(response.headers);
            if (!response.body.empty()) {
                oss << " upstream_error_body=" << truncate_for_log(response.body);
            }
            oss << " stream_callback_error=" << ex.what();
            log_error("upstream", oss.str());
            throw;
        }

        response.body = body;
        return response;
    }
}

HttpTransport::HttpTransport(const std::string &base_url,
                             int timeout_ms,
                             std::vector<HttpHeader> default_headers)
// transport 在构造时就把 URL 解析好，后续请求无需重复拆分。
    : base_(parse_base_url(base_url)),
      timeout_ms_(timeout_ms),
      default_headers_(std::move(default_headers)) {
}

HttpResponse HttpTransport::post_json(const std::string &path,
                                      const nlohmann::json &payload,
                                      const std::vector<HttpHeader> &extra_headers) const {
    // 普通 JSON 请求统一走这一套头部和 body 构造逻辑。
    const std::string target = build_target(path);
    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, base_.host);
    req.set(http::field::user_agent, "QtRAG-Server");
    req.set(http::field::content_type, "application/json");
    set_request_headers(req, default_headers_);
    set_request_headers(req, extra_headers);
    // nlohmann::json 统一负责序列化成字符串 body。
    req.body() = payload.dump();
    req.prepare_payload();

    try {
        // 每次请求单独创建 io_context，保持当前同步实现简单直接。
        const auto start_time = std::chrono::steady_clock::now();
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve(base_.host, base_.port);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;

        if (!base_.use_tls) {
            // 本地 Ollama 这类 http 上游直接走明文 TCP。
            beast::tcp_stream stream(ioc);
            stream.expires_after(std::chrono::milliseconds(timeout_ms_));
            stream.connect(results);
            http::write(stream, req);
            http::read(stream, buffer, res);

            beast::error_code ec;
            // 主动关闭发送方向即可；这里忽略 shutdown 阶段的非关键错误。
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        } else {
#ifdef QTRAG_SERVER_HAS_OPENSSL
            // OpenAI 之类 https 上游需要做 TLS 握手和主机名校验。
            ssl::context ssl_ctx(ssl::context::tls_client);
            ssl_ctx.set_default_verify_paths();

            beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
            stream.set_verify_mode(ssl::verify_peer);
            stream.set_verify_callback(ssl::host_name_verification(base_.host));

            // SNI 对大多数现代 HTTPS 服务是必需的。
            if (!SSL_set_tlsext_host_name(stream.native_handle(), base_.host.c_str())) {
                throw std::runtime_error("failed to set tls server name");
            }

            beast::get_lowest_layer(stream).expires_after(std::chrono::milliseconds(timeout_ms_));
            beast::get_lowest_layer(stream).connect(results);
            stream.handshake(ssl::stream_base::client);
            http::write(stream, req);
            http::read(stream, buffer, res);

            beast::error_code ec;
            stream.shutdown(ec);
            if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
                ec = {};
            }
            if (ec) {
                throw beast::system_error(ec);
            }
#else
            // 当前构建未带 OpenSSL 时，明确禁止走 https。
            throw std::runtime_error("https transport requires OpenSSL support");
#endif
        }

        HttpResponse response;
        response.status_code = static_cast<unsigned>(res.result_int());
        response.body = res.body();
        response.headers = collect_headers(res);
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        log_upstream_response("POST", target, response, elapsed_ms, false);
        return response;
    } catch (const std::exception &ex) {
        throw_transport_error("POST", target, ex);
    }
}

HttpResponse HttpTransport::post_json_sse(
    const std::string &path,
    const nlohmann::json &payload,
    const std::function<void(const std::string &event_name, const std::string &data)> &on_event,
    const std::vector<HttpHeader> &extra_headers) const {
    // SSE 请求除 content-type 外，还需要显式声明 accept: text/event-stream。
    const std::string target = build_target(path);
    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, base_.host);
    req.set(http::field::user_agent, "QtRAG-Server");
    req.set(http::field::content_type, "application/json");
    req.set(http::field::accept, "text/event-stream");
    set_request_headers(req, default_headers_);
    set_request_headers(req, extra_headers);
    // SSE 请求体仍然是普通 JSON，只是响应模式不同。
    req.body() = payload.dump();
    req.prepare_payload();

    try {
        const auto start_time = std::chrono::steady_clock::now();
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve(base_.host, base_.port);

        if (!base_.use_tls) {
            // HTTP SSE 直接在明文连接上持续读事件。
            beast::tcp_stream stream(ioc);
            stream.expires_after(std::chrono::milliseconds(timeout_ms_));
            stream.connect(results);
            HttpResponse response = read_sse_response(stream, req, on_event);

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            log_upstream_response("POST", target, response, elapsed_ms, true);
            return response;
        }
#ifdef QTRAG_SERVER_HAS_OPENSSL
        // HTTPS SSE 和普通 HTTPS 一样先握手，再按 chunk 持续读取事件。
        ssl::context ssl_ctx(ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();

        beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
        stream.set_verify_mode(ssl::verify_peer);
        stream.set_verify_callback(ssl::host_name_verification(base_.host));

        // HTTPS 流式和普通 HTTPS 一样需要设置 SNI。
        if (!SSL_set_tlsext_host_name(stream.native_handle(), base_.host.c_str())) {
            throw std::runtime_error("failed to set tls server name");
        }

        beast::get_lowest_layer(stream).expires_after(std::chrono::milliseconds(timeout_ms_));
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);
        HttpResponse response = read_sse_response(stream, req, on_event);

        beast::error_code ec;
        stream.shutdown(ec);
        if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
            ec = {};
        }
        if (ec) {
            throw beast::system_error(ec);
        }
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        log_upstream_response("POST", target, response, elapsed_ms, true);
        return response;
#else
        // 没有 OpenSSL 时不能消费任何 https 事件流。
        throw std::runtime_error("https transport requires OpenSSL support");
#endif
    } catch (const std::exception &ex) {
        throw_transport_error("POST", target, ex);
    }
}

HttpResponse HttpTransport::post_json_lines(
    const std::string &path,
    const nlohmann::json &payload,
    const std::function<void(const std::string &line)> &on_line,
    const std::vector<HttpHeader> &extra_headers) const {
    // 按行流式同样是普通 JSON 请求体，只是响应按换行持续返回。
    const std::string target = build_target(path);
    http::request<http::string_body> req{http::verb::post, target, 11};
    req.set(http::field::host, base_.host);
    req.set(http::field::user_agent, "QtRAG-Server");
    req.set(http::field::content_type, "application/json");
    set_request_headers(req, default_headers_);
    set_request_headers(req, extra_headers);
    req.body() = payload.dump();
    req.prepare_payload();

    try {
        const auto start_time = std::chrono::steady_clock::now();
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        auto const results = resolver.resolve(base_.host, base_.port);

        if (!base_.use_tls) {
            beast::tcp_stream stream(ioc);
            stream.expires_after(std::chrono::milliseconds(timeout_ms_));
            stream.connect(results);
            HttpResponse response = read_line_stream_response(stream, req, on_line);

            beast::error_code ec;
            stream.socket().shutdown(tcp::socket::shutdown_both, ec);
            const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            log_upstream_response("POST", target, response, elapsed_ms, true);
            return response;
        }

#ifdef QTRAG_SERVER_HAS_OPENSSL
        ssl::context ssl_ctx(ssl::context::tls_client);
        ssl_ctx.set_default_verify_paths();

        beast::ssl_stream<beast::tcp_stream> stream(ioc, ssl_ctx);
        stream.set_verify_mode(ssl::verify_peer);
        stream.set_verify_callback(ssl::host_name_verification(base_.host));

        if (!SSL_set_tlsext_host_name(stream.native_handle(), base_.host.c_str())) {
            throw std::runtime_error("failed to set tls server name");
        }

        beast::get_lowest_layer(stream).expires_after(std::chrono::milliseconds(timeout_ms_));
        beast::get_lowest_layer(stream).connect(results);
        stream.handshake(ssl::stream_base::client);
        HttpResponse response = read_line_stream_response(stream, req, on_line);

        beast::error_code ec;
        stream.shutdown(ec);
        if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
            ec = {};
        }
        if (ec) {
            throw beast::system_error(ec);
        }
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        log_upstream_response("POST", target, response, elapsed_ms, true);
        return response;
#else
        throw std::runtime_error("https transport requires OpenSSL support");
#endif
    } catch (const std::exception &ex) {
        throw_transport_error("POST", target, ex);
    }
}

HttpTransport::ParsedBaseUrl HttpTransport::parse_base_url(const std::string &base_url) {
    if (base_url.empty()) {
        throw std::invalid_argument("provider base url must not be empty");
    }

    // 要求显式带协议，避免 http/https 推断错误。
    const std::size_t scheme_pos = base_url.find("://");
    if (scheme_pos == std::string::npos) {
        throw std::invalid_argument("provider base url must contain scheme");
    }

    ParsedBaseUrl parsed;
    parsed.scheme = to_lower_copy(base_url.substr(0, scheme_pos));
    parsed.use_tls = (parsed.scheme == "https");
    if (parsed.scheme != "http" && parsed.scheme != "https") {
        throw std::invalid_argument("provider base url scheme must be http or https");
    }

    const std::string remainder = base_url.substr(scheme_pos + 3);
    const std::size_t path_pos = remainder.find('/');
    // authority 形如 host:port，base_path 是 URL 中自带的前缀路径。
    const std::string authority = (path_pos == std::string::npos) ? remainder : remainder.substr(0, path_pos);
    parsed.base_path = (path_pos == std::string::npos) ? "" : remainder.substr(path_pos);

    if (authority.empty()) {
        throw std::invalid_argument("provider base url host must not be empty");
    }

    // 未显式给端口时，按协议选择默认端口。
    const std::size_t colon_pos = authority.rfind(':');
    if (colon_pos != std::string::npos && authority.find(']') == std::string::npos) {
        parsed.host = authority.substr(0, colon_pos);
        parsed.port = authority.substr(colon_pos + 1);
    } else {
        parsed.host = authority;
        parsed.port = parsed.use_tls ? "443" : "80";
    }

    if (parsed.host.empty()) {
        throw std::invalid_argument("provider base url host must not be empty");
    }

    if (!parsed.base_path.empty() && parsed.base_path.back() == '/') {
        parsed.base_path.pop_back();
    }
    return parsed;
}

std::string HttpTransport::build_target(const std::string &path) const {
    // 统一补齐前导斜杠，避免调用方传相对路径时拼接出错。
    std::string normalized_path = path.empty() ? "/" : path;
    if (normalized_path.front() != '/') {
        normalized_path.insert(normalized_path.begin(), '/');
    }
    if (base_.base_path.empty()) {
        return normalized_path;
    }
    // base_url 自带路径前缀时，把它和目标 path 拼成最终请求 target。
    return base_.base_path + normalized_path;
}
