//
// Created by hyp on 2026/3/25.
//

#pragma once
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
class HttpServer {
public:
    HttpServer(const std::string &address, unsigned short port);

    void run();

private:
    using Request = boost::beast::http::request<boost::beast::http::string_body>;
    using Response = boost::beast::http::response<boost::beast::http::string_body>;
private:
    void do_accept_loop();

    void handle_connection(boost::asio::ip::tcp::socket socket);

    Response handle_request(const Request &req);
private:
    std::string address_;
    unsigned short port_;
    boost::asio::io_context ioc_;
};
