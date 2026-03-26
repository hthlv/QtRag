//
// Created by hyp on 2026/3/25.
//

#include "http_server.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <sstream>
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {
    // 把枚举形式的 HTTP 方法转成便于日志输出的字符串。
    std::string method_to_string(http::verb method) {
        return std::string(http::to_string(method));
    }

    // 输出普通运行日志。
    void log_info(const std::string &message) {
        std::cout << "[INFO] " << message << "\n";
    }

    // 输出错误日志。
    void log_error(const std::string &message) {
        std::cerr << "[ERROR] " << message << "\n";
    }

    // 构造统一格式的 JSON 响应，并自动补齐 Content-Length。
    http::response<http::string_body> make_json_response(unsigned version,
                                                         http::status status,
                                                         const std::string &body) {
        http::response<http::string_body> res;
        res.version(version);
        res.result(status);
        res.set(http::field::server, "QtRAG-Server");
        res.set(http::field::content_type, "application/json");
        res.keep_alive(false);
        res.body() = body;
        res.prepare_payload();
        return res;
    }
}

HttpServer::HttpServer(const std::string &address, unsigned short port)
    : address_(address),
      port_(port),
      ioc_(1) {
    // 当前 io_context 只配置一个执行线程，足够支撑最小原型。
}

void HttpServer::run() {
    std::ostringstream oss;
    oss << "Starting server on " << address_ << ":" << port_ << "\n";
    log_info(oss.str());

    // 当前版本不引入复杂的异步分发，直接进入同步 accept 循环。
    do_accept_loop();
}

void HttpServer::do_accept_loop() {
    auto ip_address = asio::ip::make_address(address_);
    tcp::endpoint endpoint{ip_address, port_};
    tcp::acceptor acceptor{ioc_, endpoint};
    std::ostringstream oss;
    oss << "Server listening at http://" << address_ << ":" << port_ << "\n";
    log_info(oss.str());
    for (;;) {
        tcp::socket socket{ioc_};
        acceptor.accept(socket);

        // 当前实现一次处理一个连接，后续可以替换成线程池或协程模型。
        handle_connection(std::move(socket));
    }
}

void HttpServer::handle_connection(tcp::socket socket) {
    try {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;

        // 先完整读取一个请求，再交给统一路由函数处理。
        http::read(socket, buffer, req);
        std::ostringstream oss;
        oss << "Incoming request: " << method_to_string(req.method()) << " " << req.target();
        log_info(oss.str());
        auto res = handle_request(req);

        // 先把响应完整写回，再主动关闭发送方向。
        http::write(socket, res);
        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
    } catch (const std::exception &ex) {
        std::ostringstream oss;
        oss << "Connection handling failed: " << ex.what();
        log_error(oss.str());
    }
}

HttpServer::Response HttpServer::handle_request(const Request &req) {
    // 健康检查接口，便于确认服务是否正常存活。
    if (req.method() == http::verb::get && req.target() == "/health") {
        return make_json_response(
            req.version(),
            http::status::ok,
            R"({"status":"ok"})");
    }

    // Echo 接口主要用于联调，直接把请求体回传给客户端。
    if (req.method() == http::verb::post && req.target() == "/echo") {
        std::string body = req.body();

        // 这里直接拼接 JSON 仅用于原型验证，正式版本应做转义处理。
        std::string json = std::string(R"({"message":"echo endpoint","body":)") +
            "\"" + body + "\"}";
        return make_json_response(
            req.version(),
            http::status::ok,
            json);
    }

    // 对已支持的方法返回 404，说明路径不存在。
    if (req.method() == http::verb::get || req.method() == http::verb::post) {
        return make_json_response(
            req.version(),
            http::status::not_found,
            R"({"error":"not found"})");
    }

    // 其他 HTTP 方法当前未实现。
    return make_json_response(
        req.version(),
        http::status::method_not_allowed,
        R"({"error":"method not allowed"})");
}
