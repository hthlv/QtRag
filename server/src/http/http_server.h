//
// Created by hyp on 2026/3/25.
//

#pragma once
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>

// 基于 Boost.Beast 的最小 HTTP 服务器，当前以同步方式处理请求。
class HttpServer {
public:
    // address 和 port 共同决定监听地址，例如 127.0.0.1:8080。
    HttpServer(const std::string &address, unsigned short port);

    // 启动监听循环，阻塞当前线程直到进程退出。
    void run();

private:
    // 当前版本统一使用字符串请求体，便于直接返回 JSON 文本。
    using Request = boost::beast::http::request<boost::beast::http::string_body>;
    using Response = boost::beast::http::response<boost::beast::http::string_body>;
private:
    // 接收新的 TCP 连接。
    void do_accept_loop();

    // 读取单个连接上的 HTTP 请求并返回响应。
    void handle_connection(boost::asio::ip::tcp::socket socket);

    // 根据路由和方法生成响应内容。
    Response handle_request(const Request &req);
private:
    // 监听绑定的 IP 地址字符串。
    std::string address_;

    // 监听端口。
    unsigned short port_;

    // 单线程 io_context，当前只服务同步 accept/read/write。
    boost::asio::io_context ioc_;
};
