//
// Created by hyp on 2026/3/28.
//

#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http.hpp>
#include <functional>
#include <string>
#include <vector>

// 轻量级 HTTP Router：
// 1. 负责 method + path 的精确匹配；
// 2. 同时支持普通 JSON 接口和 SSE 这类直接写 socket 的流式接口；
// 3. 只做路由，不关心业务细节。
class HttpRouter {
public:
    using Request = boost::beast::http::request<boost::beast::http::string_body>;
    using Response = boost::beast::http::response<boost::beast::http::string_body>;
    using JsonHandler = std::function<Response(const Request &)>;
    using StreamHandler = std::function<void(boost::asio::ip::tcp::socket &, const Request &)>;

    // 路由类型：普通响应 or 流式响应。
    enum class RouteType {
        Json,
        Stream,
    };

    // 路由匹配结果。
    struct RouteMatch {
        bool found{false};
        RouteType type{RouteType::Json};
        JsonHandler json_handler;
        StreamHandler stream_handler;
    };

    // 注册普通 JSON 路由。
    void add_json_route(boost::beast::http::verb method,
                        const std::string &path,
                        JsonHandler handler);

    // 注册流式路由，例如 SSE。
    void add_stream_route(boost::beast::http::verb method,
                          const std::string &path,
                          StreamHandler handler);

    // 根据 method + target 查找路由；target 里的 query string 会被自动剥离。
    RouteMatch match(boost::beast::http::verb method,
                     boost::beast::string_view target) const;

    // 判断当前 method 是否至少注册过一条路由。
    bool has_method(boost::beast::http::verb method) const;

private:
    struct RouteEntry {
        boost::beast::http::verb method;
        std::string path;
        RouteType type;
        JsonHandler json_handler;
        StreamHandler stream_handler;
    };

    // 统一把 target 归一化成纯 path，避免 query 参数影响精确匹配。
    static std::string normalize_target(boost::beast::string_view target);

private:
    std::vector<RouteEntry> routes_;
};
