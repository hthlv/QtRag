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
    std::string method_to_string(http::verb method) {
        return std::string(http::to_string(method));
    }

    void log_info(const std::string &message) {
        std::cout << "[INFO] " << message << "\n";
    }

    void log_error(const std::string &message) {
        std::cerr << "[ERROR] " << message << "\n";
    }

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
}

void HttpServer::run() {
    std::ostringstream oss;
    oss << "Starting server on " << address_ << ":" << port_ << "\n";
    log_info(oss.str());
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
        handle_connection(std::move(socket));
    }
}

void HttpServer::handle_connection(tcp::socket socket) {
    try {
        beast::flat_buffer buffer;
        http::request<http::string_body> req;
        http::read(socket, buffer, req);
        std::ostringstream oss;
        oss << "Incoming request: " << method_to_string(req.method()) << " " << req.target();
        log_info(oss.str());
        http::response<http::string_body> res;
        res.version(req.version());
        res.set(http::field::server, "QtRAG-Server");
        res.set(http::field::content_type, "application/json");
        res.keep_alive(false);
        if (req.method() == http::verb::get && req.target() == "/health") {
            res = make_json_response(req.version(), http::status::ok, R"({"status":"ok"})");
        } else {
            res = make_json_response(req.version(), http::status::not_found, R"({"status":"not_found"})");
        }
        res.prepare_payload();
        http::write(socket, res);
        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
    } catch (const std::exception &ex) {
        std::ostringstream oss;
        oss << "Connection handling failed: " << ex.what();
        log_error(oss.str());
    }
}
