#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <string>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

int main()
{
    try {
        asio::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {tcp::v4(), 8080}};
        std::cout << "Server listening on http://127.0.0.1:8080\n";
        for (;;) {
            tcp::socket socket{ioc};
            acceptor.accept(socket);
            beast::flat_buffer buffer;
            http::request<http::string_body> req;
            http::read(socket, buffer, req);
            http::response<http::string_body> res;
            res.version(req.version());
            res.set(http::field::server, "QtRAG-Server");
            res.set(http::field::content_type, "application/json");
            res.keep_alive(false);
            if (req.method() == http::verb::get && req.target() == "/health") {
                res.result(http::status::ok);
                res.body() = R"({"status":"ok"})";
                res.content_length(res.body().size());
            } else {
                res.result(http::status::not_found);
                res.body() = R"({"error":"not_found"})";
                res.content_length(res.body().size());

            }
            res.prepare_payload();
            http::write(socket, res);
            beast::error_code ec;
            socket.shutdown(tcp::socket::shutdown_send, ec);
        }
    } catch (const std::exception &e) {
        std::cerr << "Server error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
