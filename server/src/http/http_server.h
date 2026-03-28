//
// Created by hyp on 2026/3/25.
//

#pragma once
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <string>
#include <memory>

#include "config/app_config.h"

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
    // 接收新的 TCP 连接。
    void do_accept_loop();

    // 读取单个连接上的 HTTP 请求并返回响应。
    void handle_connection(boost::asio::ip::tcp::socket socket);

    // 统一请求分发入口
    Response handle_request(const Request &req);

    // 处理上传文档
    Response handle_upload_document(const Request &req);

    // 处理获取文档列表
    Response handle_list_documents(const Request &req);

    // 检索接口
    Response handle_retrieve(const Request &req);

    // 非流式 chat 接口
    Response handle_chat(const Request &req);

    // SSE 流式 chat
    void handle_chat_stream(boost::asio::ip::tcp::socket &socket, const Request &req);

    // 返回错误响应
    Response make_error_response(unsigned version, boost::beast::http::status status, unsigned code,
                                 const std::string &message);

private:
    // 监听绑定的 IP 地址字符串。
    std::string address_;

    // 监听端口。
    unsigned short port_;

    boost::asio::io_context ioc_;
    // worker 线程池
    boost::asio::thread_pool worker_pool_;
    std::size_t worker_threads_{0};
    std::mutex db_mutex_;

    sqlite3 *db_{nullptr};
    // 检索相关组件
    std::unique_ptr<EmbeddingClient> embedding_client_;
    std::unique_ptr<InMemoryVectorStore> vector_store_;
    std::unique_ptr<Retriever> retriever_;
    // prompt 与 LLM
    std::unique_ptr<PromptBuilder> prompt_builder_;
    std::unique_ptr<LLMClient> llm_client_;
};
