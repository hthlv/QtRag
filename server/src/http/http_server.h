//
// Created by hyp on 2026/3/25.
//

#pragma once
#include <boost/asio.hpp>

class HttpServer {
public:
    HttpServer(const std::string &address, unsigned short port);

    void run();

private:
    void do_accept_loop();

    void handle_connection(boost::asio::ip::tcp::socket socket);

private:
    std::string address_;
    unsigned short port_;
    boost::asio::io_context ioc_;
};
