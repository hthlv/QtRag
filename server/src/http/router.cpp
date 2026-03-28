//
// Created by hyp on 2026/3/28.
//

#include "router.h"

void HttpRouter::add_json_route(boost::beast::http::verb method,
                                const std::string &path,
                                JsonHandler handler) {
    RouteEntry entry;
    entry.method = method;
    entry.path = path;
    entry.type = RouteType::Json;
    entry.json_handler = std::move(handler);
    routes_.push_back(std::move(entry));
}

void HttpRouter::add_stream_route(boost::beast::http::verb method,
                                  const std::string &path,
                                  StreamHandler handler) {
    RouteEntry entry;
    entry.method = method;
    entry.path = path;
    entry.type = RouteType::Stream;
    entry.stream_handler = std::move(handler);
    routes_.push_back(std::move(entry));
}

HttpRouter::RouteMatch HttpRouter::match(boost::beast::http::verb method,
                                         boost::beast::string_view target) const {
    const std::string normalized_target = normalize_target(target);
    for (const auto &route : routes_) {
        if (route.method != method || route.path != normalized_target) {
            continue;
        }

        RouteMatch match_result;
        match_result.found = true;
        match_result.type = route.type;
        match_result.json_handler = route.json_handler;
        match_result.stream_handler = route.stream_handler;
        return match_result;
    }

    return {};
}

bool HttpRouter::has_method(boost::beast::http::verb method) const {
    for (const auto &route : routes_) {
        if (route.method == method) {
            return true;
        }
    }
    return false;
}

std::string HttpRouter::normalize_target(boost::beast::string_view target) {
    const std::size_t query_pos = target.find('?');
    if (query_pos == boost::beast::string_view::npos) {
        return std::string(target);
    }
    return std::string(target.substr(0, query_pos));
}
