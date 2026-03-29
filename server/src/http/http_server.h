//
// Created by hyp on 2026/3/25.
//

#pragma once
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "config/app_config.h"
#include "router.h"

// 前向声明
struct sqlite3;
class EmbeddingClient;
class InMemoryVectorStore;
class Retriever;
class PromptBuilder;
class LLMClient;

// 基于 Boost.Beast 的最小 HTTP 服务器，当前以同步方式处理请求。
class HttpServer {
public:
    HttpServer(const AppConfig &config, sqlite3 *db);

    ~HttpServer();

    // 启动监听循环，阻塞当前线程直到进程退出。
    void run();

    // 服务启动前，从数据库恢复向量索引
    void initialize_index_from_storage();

public:
    // 当前版本统一使用字符串请求体，便于直接返回 JSON 文本。
    using Request = boost::beast::http::request<boost::beast::http::string_body>;
    using Response = boost::beast::http::response<boost::beast::http::string_body>;

private:
    // 启动时注册所有 HTTP 路由。
    void register_routes();

    // 接收新的 TCP 连接。
    void do_accept_loop();

    // 读取单个连接上的 HTTP 请求；这里只做收包和路由前置，不在这里执行慢业务。
    void handle_connection(boost::asio::ip::tcp::socket socket);

    // 按请求类型把业务处理投递到合适的执行器，避免 HTTP worker 被慢任务长期占满。
    // 这里会把 socket 和已读完的请求对象一起 move 到目标线程池。
    void dispatch_request(boost::asio::ip::tcp::socket socket,
                          Request req,
                          const HttpRouter::RouteMatch &matched_route);

    // 真正执行路由处理并负责写回响应；可能运行在 HTTP worker 或专用后台线程池里。
    // 普通 JSON 路由和 SSE 路由都会在这里汇合，便于统一处理写回和连接关闭逻辑。
    void handle_request(boost::asio::ip::tcp::socket socket,
                        Request req,
                        const HttpRouter::RouteMatch &matched_route);

    // 对未匹配路由的请求生成统一错误响应。
    Response make_route_error_response(const Request &req);

    // 从持久化 embedding 表重新加载内存向量索引。
    void reload_vector_store_from_storage();

    // 处理上传文档
    Response handle_upload_document(const Request &req);

    // 处理获取文档列表
    Response handle_list_documents(const Request &req);

    // 返回当前可选聊天模型列表。
    Response handle_list_models(const Request &req);

    // 检索接口
    Response handle_retrieve(const Request &req);

    // 非流式 chat 接口
    Response handle_chat(const Request &req);

    // 重新生成 embedding，支持单文档和全量重建。
    Response handle_regenerate_embeddings(const Request &req);

    // SSE 流式 chat
    void handle_chat_stream(boost::asio::ip::tcp::socket &socket, const Request &req);

    // 删除文档
    Response handle_remove_document(const Request &req);

    // 返回错误响应
    Response make_error_response(unsigned version, boost::beast::http::status status, unsigned code,
                                 const std::string &message);

    // 解析当前请求使用的 LLM；未指定时回退到默认模型。
    const LLMClient *resolve_llm_client(const Request &req, std::string *resolved_llm_id) const;

private:
    // 监听绑定的 IP 地址字符串。
    std::string address_;

    // 监听端口。
    unsigned short port_;

    boost::asio::io_context ioc_;
    // HTTP worker 只负责收包、解析和快速分发，尽快把连接交给专用执行器。
    boost::asio::thread_pool http_worker_pool_;
    // chat / retrieve / stream 这类强依赖上游模型响应的慢请求，统一放到独立执行器。
    boost::asio::thread_pool upstream_worker_pool_;
    // 文档上传、重建 embedding 这类重 CPU + 重 I/O 任务，避免与 chat 互相抢占。
    boost::asio::thread_pool document_worker_pool_;
    std::size_t worker_threads_{0};
    std::mutex db_mutex_;

    sqlite3 *db_{nullptr};
    // 检索相关组件
    std::unique_ptr<EmbeddingClient> embedding_client_;
    std::unique_ptr<InMemoryVectorStore> vector_store_;
    std::unique_ptr<Retriever> retriever_;
    // prompt 与 LLM
    std::unique_ptr<PromptBuilder> prompt_builder_;
    std::unordered_map<std::string, std::unique_ptr<LLMClient>> llm_clients_;
    std::vector<LlmOptionConfig> llm_options_;
    std::string default_llm_id_;
    // 独立 router，负责 method + path 到 handler 的映射关系。
    HttpRouter router_;
};
