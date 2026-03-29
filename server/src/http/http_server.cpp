//
// Created by hyp on 2026/3/25.
//

#include "http_server.h"
#include "adapters/embedding_client.h"
#include "adapters/llm_client.h"
#include "adapters/provider_factory.h"
#include "core/chunker.h"
#include "core/retriever.h"
#include "core/in_memory_vector_store.h"
#include "core/prompt_builder.h"
#include "models/document_record.h"
#include "models/chunk_record.h"
#include "models/embedding_record.h"
#include "storage/repositories/document_repository.h"
#include "storage/repositories/chunk_repository.h"
#include "storage/repositories/embedding_repository.h"
#include "utils/logger.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <sstream>
#include <chrono>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <string_view>
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {
    constexpr unsigned kErrorInvalidParameter = 4001;
    constexpr unsigned kErrorMissingHeader = 4002;
    constexpr unsigned kErrorEmptyBody = 4003;
    constexpr unsigned kErrorDatabase = 5001;
    constexpr unsigned kErrorEmbedding = 5002;
    constexpr unsigned kErrorLlm = 5003;
    constexpr unsigned kErrorInternal = 5004;

    struct ErrorInfo {
        unsigned code;
        std::string message;
    };

    // JSON字符串转义
    std::string escape_json(const std::string &input) {
        std::string output;
        output.reserve(input.size() + 16);
        for (char ch: input) {
            switch (ch) {
                case '\"': output += "\\\"";
                    break;
                case '\\': output += "\\\\";
                    break;
                case '\n': output += "\\n";
                    break;
                case '\r': output += "\\r";
                    break;
                case '\t': output += "\\t";
                    break;
                default: output += ch;
                    break;
            }
        }

        return output;
    }

    std::string build_error_json(unsigned code, const std::string &message) {
        std::ostringstream oss;
        oss << "{"
                << R"("code":)" << code << ","
                << R"("message":")" << escape_json(message) << "\""
                << "}";
        return oss.str();
    }

    // 把枚举形式的 HTTP 方法转成便于日志输出的字符串。
    std::string method_to_string(http::verb method) {
        return std::string(http::to_string(method));
    }

    std::string to_lower_copy(const std::string &text) {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return lower;
    }

    bool starts_with(const std::string &text, const std::string &prefix) {
        return text.size() >= prefix.size() &&
               text.compare(0, prefix.size(), prefix) == 0;
    }

    ErrorInfo classify_internal_error(const std::exception &ex) {
        const std::string raw = ex.what();
        const std::string normalized = to_lower_copy(raw);

        if (normalized.find("sqlite") != std::string::npos ||
            starts_with(normalized, "prepare ") ||
            starts_with(normalized, "insert ") ||
            starts_with(normalized, "update ") ||
            starts_with(normalized, "find ") ||
            starts_with(normalized, "list ")) {
            return {kErrorDatabase, "database error"};
        }
        if (normalized.find("embedding request failed") != std::string::npos ||
            normalized.find("embedding response format is invalid") != std::string::npos ||
            normalized.find("embedding input must be valid utf-8 text") != std::string::npos) {
            return {kErrorEmbedding, "embedding call failed"};
        }
        if (normalized.find("llm request failed") != std::string::npos ||
            normalized.find("llm response format is invalid") != std::string::npos) {
            return {kErrorLlm, "llm call failed"};
        }
        return {kErrorInternal, "internal error"};
    }

    bool is_utf8_continuation_byte(unsigned char ch) {
        return (ch & 0xC0) == 0x80;
    }

    // 只接受合法 UTF-8 文本，避免后续构造 embedding 请求 JSON 时抛编码异常。
    bool is_valid_utf8(std::string_view text) {
        std::size_t i = 0;
        while (i < text.size()) {
            const unsigned char ch = static_cast<unsigned char>(text[i]);
            if ((ch & 0x80) == 0x00) {
                ++i;
                continue;
            }

            if ((ch & 0xE0) == 0xC0) {
                if (i + 1 >= text.size()) {
                    return false;
                }
                const unsigned char ch1 = static_cast<unsigned char>(text[i + 1]);
                if (ch < 0xC2 || !is_utf8_continuation_byte(ch1)) {
                    return false;
                }
                i += 2;
                continue;
            }

            if ((ch & 0xF0) == 0xE0) {
                if (i + 2 >= text.size()) {
                    return false;
                }
                const unsigned char ch1 = static_cast<unsigned char>(text[i + 1]);
                const unsigned char ch2 = static_cast<unsigned char>(text[i + 2]);
                if (!is_utf8_continuation_byte(ch1) || !is_utf8_continuation_byte(ch2)) {
                    return false;
                }
                if ((ch == 0xE0 && ch1 < 0xA0) || (ch == 0xED && ch1 >= 0xA0)) {
                    return false;
                }
                i += 3;
                continue;
            }

            if ((ch & 0xF8) == 0xF0) {
                if (i + 3 >= text.size()) {
                    return false;
                }
                const unsigned char ch1 = static_cast<unsigned char>(text[i + 1]);
                const unsigned char ch2 = static_cast<unsigned char>(text[i + 2]);
                const unsigned char ch3 = static_cast<unsigned char>(text[i + 3]);
                if (!is_utf8_continuation_byte(ch1) ||
                    !is_utf8_continuation_byte(ch2) ||
                    !is_utf8_continuation_byte(ch3)) {
                    return false;
                }
                if (ch > 0xF4 || (ch == 0xF0 && ch1 < 0x90) || (ch == 0xF4 && ch1 > 0x8F)) {
                    return false;
                }
                i += 4;
                continue;
            }

            return false;
        }

        return true;
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

    // 获取当前秒级时间戳
    std::int64_t current_timestamp() {
        return static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    }

    // 生成一个简单的文档ID
    std::string generate_docuemtn_id() {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        return "doc_" + std::to_string(now);
    }

    // 生成 chunk id
    std::string generate_chunk_id() {
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        return "chunk_" + std::to_string(now);
    }

    // 清洗文件名，防止路径穿越
    // 只保留字母、数字、点、横杠、下划线、空格
    std::string sanitize_filename(const std::string &filename) {
        std::string result;
        result.reserve(filename.size());
        for (unsigned char ch: filename) {
            if (ch == '/' || ch == '\\' || ch < 0x20 || ch == 0x7F) {
                continue;
            }
            result.push_back(static_cast<char>(ch));
        }

        // 如果过滤后为空，给个默认文件名
        if (result.empty()) {
            result = "unnamed.txt";
        }

        return result;
    }

    // 从请求头读取自定义header
    std::string get_header_value(const HttpServer::Request &req, const std::string &key) {
        auto it = req.find(key);
        if (it == req.end()) {
            return "";
        }
        return std::string(it->value());
    }

    // 将文本写入文件
    bool write_file(const std::string &path, const std::string &content) {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            return false;
        }
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        return ofs.good();
    }

    // 删除本地文件
    bool delete_file(const std::string &path) {
        return std::filesystem::remove(path);
    }

    // 将文件列表拼成 json
    std::string build_document_list_json(const std::vector<DocumentRecord> &docs) {
        std::ostringstream oss;
        oss << R"({"items":[)";
        for (std::size_t i = 0; i < docs.size(); ++i) {
            const auto &doc = docs[i];
            if (i > 0) {
                oss << ",";
            }
            oss << "{"
                    << R"("id":")" << escape_json(doc.id) << "\","
                    << R"("kb_id":")" << escape_json(doc.kb_id) << "\","
                    << R"("filename":")" << escape_json(doc.filename) << "\","
                    << R"("status":")" << escape_json(doc.status) << "\","
                    << R"("chunk_count":)" << doc.chunk_count << ","
                    << R"("created_at":)" << doc.created_at
                    << "}";
        }
        oss << "]}";
        return oss.str();
    }

    // 从请求头解析 tok-k，默认值为3
    std::size_t parse_top_k(const HttpServer::Request &req, std::size_t default_value = 3) {
        auto raw = get_header_value(req, "X-Top-K");
        if (raw.empty()) {
            return default_value;
        }
        try {
            int value = std::stoi(raw);
            if (value <= 0) {
                return default_value;
            }
            return static_cast<std::size_t>(value);
        } catch (...) {
            return default_value;
        }
    }

    // 将检索结果拼成 JSON
    std::string build_retrieve_result_json(const std::vector<RetrievedChunk> &items) {
        std::ostringstream oss;
        oss << R"({"items":[)";
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto &item = items[i];
            if (i > 0) {
                oss << ",";
            }
            oss << "{"
                    << R"("chunk_id":")" << escape_json(item.chunk_id) << "\","
                    << R"("doc_id":")" << escape_json(item.doc_id) << "\","
                    << R"("filename":")" << escape_json(item.filename) << "\","
                    << R"("score":)" << item.score << ","
                    << R"("text":")" << escape_json(item.text) << "\""
                    << "}";
        }
        oss << "]}";
        return oss.str();
    }

    // 将 answer + reference 拼成 chat 接口返回 json
    std::string build_chat_result_json(const std::string &answer,
                                       const std::vector<RetrievedChunk> &refs) {
        std::ostringstream oss;
        oss << "{";
        oss << R"("answer":")" << escape_json(answer) << "\",";
        oss << R"("references":[)";
        for (std::size_t i = 0; i < refs.size(); ++i) {
            const auto &item = refs[i];
            if (i > 0) {
                oss << ",";
            }
            oss << "{"
                    << R"("chunk_id":")" << escape_json(item.chunk_id) << "\","
                    << R"("doc_id":")" << escape_json(item.doc_id) << "\","
                    << R"("filename":")" << escape_json(item.filename) << "\","
                    << R"("score":)" << item.score << ","
                    << R"("text":")" << escape_json(item.text) << "\""
                    << "}";
        }
        oss << "]}";
        return oss.str();
    }

    // 将 embedding 重建结果拼成 JSON，便于客户端展示执行范围和统计数量。
    std::string build_regenerate_embeddings_result_json(const std::string &scope,
                                                        const std::string &doc_id,
                                                        std::size_t document_count,
                                                        std::size_t chunk_count) {
        std::ostringstream oss;
        oss << "{"
                << R"("scope":")" << escape_json(scope) << "\","
                << R"("document_count":)" << document_count << ","
                << R"("chunk_count":)" << chunk_count << ","
                << R"("status":"reindexed")";
        if (!doc_id.empty()) {
            oss << "," << R"("doc_id":")" << escape_json(doc_id) << "\"";
        }
        oss << "}";
        return oss.str();
    }

    // 将 references 单独拼成 JSON，供 SSE refs 事件使用
    std::string build_references_json(const std::vector<RetrievedChunk> &refs) {
        std::ostringstream oss;
        oss << R"({"references":[)";
        for (std::size_t i = 0; i < refs.size(); ++i) {
            const auto &item = refs[i];
            if (i > 0) {
                oss << ",";
            }
            oss << "{"
                    << R"("chunk_id":")" << escape_json(item.chunk_id) << "\","
                    << R"("doc_id":")" << escape_json(item.doc_id) << "\","
                    << R"("filename":")" << escape_json(item.filename) << "\","
                    << R"("score":)" << item.score << ","
                    << R"("text":")" << escape_json(item.text) << "\""
                    << "}";
        }
        oss << "]}";
        return oss.str();
    }

    // 把服务端可用聊天模型列表拼成 JSON，供客户端设置页选择。
    std::string build_model_list_json(const std::vector<LlmOptionConfig> &options,
                                      const std::string &default_llm_id) {
        std::ostringstream oss;
        oss << "{"
                << R"("default_llm_id":")" << escape_json(default_llm_id) << "\","
                << R"("items":[)";
        for (std::size_t i = 0; i < options.size(); ++i) {
            const auto &item = options[i];
            if (i > 0) {
                oss << ",";
            }
            oss << "{"
                    << R"("id":")" << escape_json(item.id) << "\","
                    << R"("label":")" << escape_json(item.label) << "\","
                    << R"("provider_type":")" << escape_json(item.provider.type) << "\","
                    << R"("model":")" << escape_json(item.model) << "\","
                    << R"("is_default":)" << (item.id == default_llm_id ? "true" : "false")
                    << "}";
        }
        oss << "]}";
        return oss.str();
    }

    // 写 SSE 响应头
    void write_sse_headers(tcp::socket &socket, unsigned version) {
        http::response<http::empty_body> res{http::status::ok, version};
        // SSE 必备响应头
        res.set(http::field::server, "QtRAG-Server");
        res.set(http::field::content_type, "text/event-stream; charset=utf-8");
        res.set(http::field::cache_control, "no-cache");
        res.set(http::field::connection, "close");
        // 使用 chunked 传输，便于持续输出
        res.chunked(true);
        http::serializer<false, http::empty_body> sr{res};
        http::write_header(socket, sr);
    }

    // 向客户端发送一个 SSE 事件
    void write_sse_event(tcp::socket &socket,
                         const std::string &event_name,
                         const std::string &data_json) {
        // SSE 标准格式：
        // event: xxx
        // data: yyy
        std::string payload =
                "event: " + event_name + "\n" +
                "data: " + data_json + "\n\n";
        // 以 chunk 的方式发送
        asio::write(socket, http::make_chunk(asio::buffer(payload)));
    }

    // 发送 SSE 结束块
    void write_sse_end(tcp::socket &socket) {
        asio::write(socket, http::make_chunk_last());
    }
}

HttpServer::HttpServer(const AppConfig &config, sqlite3 *db)
    : address_(config.server.listen_address),
      port_(config.server.listen_port),
      ioc_(1),
      worker_pool_(config.server.worker_threads),
      worker_threads_(config.server.worker_threads),
      db_(db),
      embedding_client_(create_embedding_client(config)),
      vector_store_(std::make_unique<InMemoryVectorStore>()),
      retriever_(std::make_unique<Retriever>(
          embedding_client_.get(),
          vector_store_.get(),
          db,
          &db_mutex_)),
      prompt_builder_(std::make_unique<PromptBuilder>()),
      llm_options_(config.llm_options),
      default_llm_id_(config.default_llm_id) {
    // 启动时一次性构建所有可选 LLM 实例，运行中按请求头快速路由。
    for (const auto &option: llm_options_) {
        llm_clients_.emplace(option.id, create_llm_client(option));
    }
    // 构造完成后统一注册所有 HTTP 路由，后续扩展接口时只需要改这里。
    register_routes();
}

HttpServer::~HttpServer() = default;


void HttpServer::run() {
    std::ostringstream oss;
    oss << "Starting server on " << address_ << ":" << port_;
    log_info("http", oss.str());

    do_accept_loop();
    worker_pool_.join();
}

void HttpServer::initialize_index_from_storage() {
    reload_vector_store_from_storage();
}

void HttpServer::reload_vector_store_from_storage() {
    // 启动阶段从数据库加载 embedding，恢复内存向量索引
    std::lock_guard<std::mutex> lock(db_mutex_);
    EmbeddingRepository embeddingRepo(db_);
    auto records = embeddingRepo.list_all();
    vector_store_->clear();
    for (const auto &record: records) {
        vector_store_->add(
            record.chunk_id,
            record.doc_id,
            record.content,
            record.embedding);
    }
    std::ostringstream oss;
    oss << "Restored persisted vector index entries: " << records.size();
    log_info("http", oss.str());
}

void HttpServer::register_routes() {
    // 健康检查。
    router_.add_json_route(http::verb::get, "/health", [this](const Request &req) {
        (void) this;
        return make_json_response(
            req.version(),
            http::status::ok,
            R"({"status":"ok"})");
    });

    // 文档接口。
    router_.add_json_route(http::verb::get, "/api/v1/docs", [this](const Request &req) {
        return handle_list_documents(req);
    });
    router_.add_json_route(http::verb::get, "/api/v1/models", [this](const Request &req) {
        return handle_list_models(req);
    });
    router_.add_json_route(http::verb::post, "/api/v1/docs/upload", [this](const Request &req) {
        return handle_upload_document(req);
    });
    router_.add_json_route(http::verb::post, "/api/v1/docs/remove", [this](const Request &req) {
        return handle_remove_document(req);
    });

    // 检索与聊天接口。
    router_.add_json_route(http::verb::post, "/api/v1/retrieve", [this](const Request &req) {
        return handle_retrieve(req);
    });
    router_.add_json_route(http::verb::post, "/api/v1/chat", [this](const Request &req) {
        return handle_chat(req);
    });
    router_.add_json_route(http::verb::post, "/api/v1/embeddings/regenerate", [this](const Request &req) {
        return handle_regenerate_embeddings(req);
    });
    router_.add_stream_route(http::verb::post, "/api/v1/chat/stream",
                             [this](tcp::socket &socket, const Request &req) {
                                 handle_chat_stream(socket, req);
                             });
}

void HttpServer::do_accept_loop() {
    auto ip_address = asio::ip::make_address(address_);
    tcp::endpoint endpoint{ip_address, port_};
    tcp::acceptor acceptor{ioc_, endpoint};
    std::ostringstream oss;
    oss << "Server listening at http://" << address_ << ":" << port_;
    log_info("http", oss.str());
    for (;;) {
        tcp::socket socket{ioc_};
        acceptor.accept(socket);
        // accept 线程只负责接收连接，真正的请求处理放进 worker 线程池执行
        asio::post(worker_pool_, [this, socket = std::move(socket)]() mutable {
            handle_connection(std::move(socket));
        });
        // handle_connection(std::move(socket));
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
        log_info("http", oss.str());

        // 所有接口都先经过 router 做 method + path 匹配。
        const auto matched_route = router_.match(req.method(), req.target());
        if (matched_route.found && matched_route.type == HttpRouter::RouteType::Stream) {
            matched_route.stream_handler(socket, req);
            beast::error_code ec;
            socket.shutdown(tcp::socket::shutdown_send, ec);
            return;
        }

        // 普通 JSON 路由直接调用 handler；未命中时返回统一错误响应。
        auto res = matched_route.found
                       ? matched_route.json_handler(req)
                       : make_route_error_response(req);
        // 先把响应完整写回，再主动关闭发送方向。
        http::write(socket, res);
        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
    } catch (const std::exception &ex) {
        std::ostringstream oss;
        oss << "Connection handling failed: " << ex.what();
        log_error("http", oss.str());
    }
}

HttpServer::Response HttpServer::make_route_error_response(const Request &req) {
    // 已注册过的 method 但路径不存在，返回 404。
    if (router_.has_method(req.method())) {
        return make_error_response(
            req.version(),
            http::status::not_found,
            kErrorInvalidParameter,
            "not found");
    }

    // 其他 HTTP 方法当前未实现。
    return make_error_response(
        req.version(),
        http::status::method_not_allowed,
        kErrorInvalidParameter,
        "method not allowed");
}

HttpServer::Response HttpServer::handle_upload_document(const Request &req) {
    // 读取自定义 header
    const std::string raw_filename = get_header_value(req, "X-Filename");
    const std::string kb_id = get_header_value(req, "X-Kb-Id").empty() ? "default" : get_header_value(req, "X-Kb-Id");
    // 简单校验：文件名不能为空
    if (raw_filename.empty()) {
        return make_error_response(
            req.version(),
            http::status::bad_request,
            kErrorMissingHeader,
            "missing X-Filename header");
    }
    // 简单校验：请求体不能为空
    if (req.body().empty()) {
        return make_error_response(
            req.version(),
            http::status::bad_request,
            kErrorEmptyBody,
            "empty request body");
    }
    if (!is_valid_utf8(req.body())) {
        return make_error_response(
            req.version(),
            http::status::bad_request,
            kErrorInvalidParameter,
            "document must be valid UTF-8 text");
    }
    // 清洗文件名，防止 ../ 路径穿越
    const std::string safe_filename = sanitize_filename(raw_filename);
    const std::string doc_id = generate_docuemtn_id();
    const std::int64_t now = current_timestamp();
    // 确保目录存在
    std::filesystem::create_directories("data/files");
    // 服务器本地保存路径
    const std::string file_path = "data/files/" + doc_id + "_" + safe_filename;
    // 写文件磁盘
    if (!write_file(file_path, req.body())) {
        return make_error_response(
            req.version(),
            http::status::internal_server_error,
            kErrorInternal,
            "failed to save file");
    }
    // 将文档元数据写入数据库
    try {
        DocumentRepository docRepo(db_);
        ChunkRepository chunkRepo(db_);
        EmbeddingRepository embeddingRepo(db_);

        // 1. 切片
        Chunker chunker(800, 150);
        auto chunks = chunker.split(req.body());
        // 2. 先在内存中准备好 chunk + embedding
        struct PreparedChunk {
            ChunkRecord chunk;
            std::vector<float> embedding;
        };
        std::vector<PreparedChunk> prepared_chunks;
        prepared_chunks.reserve(chunks.size());
        for (std::size_t i = 0; i < chunks.size(); ++i) {
            PreparedChunk item;
            item.chunk.id = doc_id + "_chunk_" + std::to_string(i);
            item.chunk.doc_id = doc_id;
            item.chunk.chunk_index = static_cast<int>(i);
            item.chunk.content = chunks[i];
            item.chunk.created_at = now;
            // 先生成 embedding
            item.embedding = embedding_client_->embed(item.chunk.content);
            prepared_chunks.push_back(std::move(item));
        }
        // 3. 写数据库：documents / chunks / chunk_embeddings
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            DocumentRecord doc;
            doc.id = doc_id;
            doc.kb_id = kb_id;
            doc.filename = safe_filename;
            doc.file_path = file_path;
            doc.status = "uploaded";
            doc.chunk_count = 0;
            doc.created_at = now;
            doc.updated_at = now;
            docRepo.insert(doc);

            for (const auto &item: prepared_chunks) {
                // 写 chunk 表
                chunkRepo.insert(item.chunk);
                // 写 embedding 表，实现索引持久化
                EmbeddingRecord embeddingRecord;
                embeddingRecord.chunk_id = item.chunk.id;
                embeddingRecord.doc_id = item.chunk.doc_id;
                embeddingRecord.content = item.chunk.content;
                embeddingRecord.embedding = item.embedding;
                embeddingRecord.created_at = now;
                embeddingRepo.insert_or_replace(embeddingRecord);
            }
            // 更新文档状态
            docRepo.update_status_and_chunk_count(
                doc_id,
                "indexed",
                static_cast<int>(prepared_chunks.size()),
                current_timestamp());
        }

        // 4. 数据库写完后，再写入内存向量索引
        for (const auto &item: prepared_chunks) {
            vector_store_->add(
                item.chunk.id,
                item.chunk.doc_id,
                item.chunk.content,
                item.embedding);
        }

        // 5. 返回成功响应
        std::ostringstream oss;
        oss << "{"
                << R"("doc_id":")" << escape_json(doc_id) << "\","
                << R"("filename":")" << escape_json(safe_filename) << "\","
                << R"("status":"indexed",)"
                << R"("chunk_count":)" << chunks.size()
                << "}";
        return make_json_response(
            req.version(),
            http::status::ok,
            oss.str());
    } catch (const std::exception &ex) {
        log_error("http", std::string("upload/index document failed: ") + ex.what());
        try {
            DocumentRepository docRepo(db_);
            docRepo.update_status_and_chunk_count(
                doc_id,
                "failed",
                0,
                current_timestamp());
        } catch (...) {
            // 这里不再继续抛异常，避免覆盖原始错误
        }
        const auto error = classify_internal_error(ex);
        return make_error_response(
            req.version(),
            http::status::internal_server_error,
            error.code,
            error.message);
    }
}

HttpServer::Response HttpServer::handle_list_documents(const Request &req) {
    try {
        std::vector<DocumentRecord> docs;
        {
            std::lock_guard<std::mutex> lock(db_mutex_);
            DocumentRepository repo(db_);
            docs = repo.list_all();
        }
        return make_json_response(
            req.version(),
            http::status::ok,
            build_document_list_json(docs));
    } catch (const std::exception &ex) {
        log_error("http", std::string("list documents failed: ") + ex.what());
        const auto error = classify_internal_error(ex);
        return make_error_response(
            req.version(),
            http::status::internal_server_error,
            error.code,
            error.message);
    }
}

HttpServer::Response HttpServer::handle_list_models(const Request &req) {
    // 给客户端返回“可选模型 + 默认模型”，供设置页展示和切换。
    return make_json_response(
        req.version(),
        http::status::ok,
        build_model_list_json(llm_options_, default_llm_id_));
}

const LLMClient *HttpServer::resolve_llm_client(const Request &req, std::string *resolved_llm_id) const {
    // 支持客户端通过 X-LLM-Id 指定模型；未指定时回退到服务端默认模型。
    std::string llm_id = get_header_value(req, "X-LLM-Id");
    if (llm_id.empty()) {
        llm_id = default_llm_id_;
    }
    const auto it = llm_clients_.find(llm_id);
    if (it == llm_clients_.end()) {
        return nullptr;
    }
    if (resolved_llm_id) {
        *resolved_llm_id = llm_id;
    }
    return it->second.get();
}

HttpServer::Response HttpServer::handle_retrieve(const Request &req) {
    // 1. 请求体直接就是 query 文本
    // 2. 可选请求头 X-Top-K 指定返回数量
    const std::string query = req.body();
    const std::size_t top_k = parse_top_k(req, 3);
    if (query.empty()) {
        return make_error_response(
            req.version(),
            http::status::bad_request,
            kErrorInvalidParameter,
            "empty query");
    }
    try {
        // 调用 retriever 做索引
        auto items = retriever_->retrieve(query, top_k);
        return make_json_response(
            req.version(),
            http::status::ok,
            build_retrieve_result_json(items));
    } catch (const std::exception &ex) {
        log_error("http", std::string("retrieve failed: ") + ex.what());
        const auto error = classify_internal_error(ex);
        return make_error_response(
            req.version(),
            http::status::internal_server_error,
            error.code,
            error.message);
    }
}

HttpServer::Response HttpServer::handle_chat(const Request &req) {
    // 1. 请求体直接放 query 文本
    // 2. X-Top-K 指定检索数量，默认 3
    const std::string query = req.body();
    const std::size_t top_k = parse_top_k(req, 3);
    std::string llm_id;
    const LLMClient *llm_client = resolve_llm_client(req, &llm_id);
    // 空 query 直接返回 400
    if (query.empty()) {
        return make_error_response(
            req.version(),
            http::status::bad_request,
            kErrorInvalidParameter,
            "empty query");
    }
    if (!llm_client) {
        // 客户端传了不存在的模型 ID，直接返回参数错误。
        return make_error_response(
            req.version(),
            http::status::bad_request,
            kErrorInvalidParameter,
            "unknown llm id");
    }
    try {
        // 1. 检索知识库中的相关片段
        auto refs = retriever_->retrieve(query, top_k);
        // 2. 构造 prompt
        std::string prompt = prompt_builder_->build(query, refs);
        // 3. 调用LLM生成回答
        std::string answer = llm_client->generate(query, refs, prompt);
        // 4. 返回 answer + reference
        return make_json_response(
            req.version(),
            http::status::ok,
            build_chat_result_json(answer, refs));
    } catch (const std::exception &ex) {
        log_error("http", std::string("chat request failed: ") + ex.what());
        const auto error = classify_internal_error(ex);
        return make_error_response(
            req.version(),
            http::status::internal_server_error,
            error.code,
            error.message);
    }
}

HttpServer::Response HttpServer::handle_regenerate_embeddings(const Request &req) {
    // 可选请求头 X-Doc-Id：不传则全量重建，传入则只重建指定文档。
    const std::string doc_id = get_header_value(req, "X-Doc-Id");

    try {
        std::vector<DocumentRecord> target_docs;
        {
            // 先在数据库里确定本次要处理哪些文档，但不把 embedding 调用放在数据库锁内。
            std::lock_guard<std::mutex> lock(db_mutex_);
            DocumentRepository doc_repo(db_);
            if (!doc_id.empty()) {
                const auto document = doc_repo.find_by_id(doc_id);
                if (!document.has_value()) {
                    return make_error_response(
                        req.version(),
                        http::status::not_found,
                        kErrorInvalidParameter,
                        "document not found");
                }
                target_docs.push_back(*document);
            } else {
                target_docs = doc_repo.list_all();
            }
        }

        ChunkRepository chunk_repo(db_);
        EmbeddingRepository embedding_repo(db_);

        std::size_t regenerated_chunk_count = 0;
        for (const auto &document: target_docs) {
            std::vector<ChunkRecord> chunks;
            {
                // 每个文档的 chunk 先读出来，后面在锁外调用 embedding provider。
                std::lock_guard<std::mutex> lock(db_mutex_);
                chunks = chunk_repo.list_by_doc_id(document.id);
            }

            std::vector<EmbeddingRecord> regenerated_records;
            regenerated_records.reserve(chunks.size());
            for (const auto &chunk: chunks) {
                EmbeddingRecord record;
                record.chunk_id = chunk.id;
                record.doc_id = chunk.doc_id;
                record.content = chunk.content;
                record.embedding = embedding_client_->embed(chunk.content);
                record.created_at = current_timestamp();
                regenerated_records.push_back(std::move(record));
            }

            {
                // embedding 结果统一回写数据库，并刷新文档状态时间戳。
                std::lock_guard<std::mutex> lock(db_mutex_);
                for (const auto &record: regenerated_records) {
                    embedding_repo.insert_or_replace(record);
                }

                DocumentRepository doc_repo(db_);
                doc_repo.update_status_and_chunk_count(
                    document.id,
                    "indexed",
                    static_cast<int>(chunks.size()),
                    current_timestamp());
            }

            regenerated_chunk_count += regenerated_records.size();
        }

        // 持久化完成后，用数据库中的最新 embedding 全量刷新内存索引，避免旧向量残留。
        reload_vector_store_from_storage();

        return make_json_response(
            req.version(),
            http::status::ok,
            build_regenerate_embeddings_result_json(
                doc_id.empty() ? "all" : "single",
                doc_id,
                target_docs.size(),
                regenerated_chunk_count));
    } catch (const std::exception &ex) {
        log_error("http", std::string("regenerate embeddings failed: ") + ex.what());
        const auto error = classify_internal_error(ex);
        return make_error_response(
            req.version(),
            http::status::internal_server_error,
            error.code,
            error.message);
    }
}

void HttpServer::handle_chat_stream(boost::asio::ip::tcp::socket &socket,
                                    const Request &req) {
    // 1. 请求体直接就是 query 文本
    // 2. X-Top-K 指定检索数量，默认 3
    const std::string query = req.body();
    const std::size_t top_k = parse_top_k(req, 3);
    std::string llm_id;
    const LLMClient *llm_client = resolve_llm_client(req, &llm_id);
    bool headers_written = false;
    try {
        // 先写 SSE 响应头
        write_sse_headers(socket, req.version());
        headers_written = true;
        // query 为空时，直接发 error + done
        if (query.empty()) {
            write_sse_event(socket, "error", build_error_json(kErrorInvalidParameter, "empty query"));
            write_sse_event(socket, "done", R"({})");
            write_sse_end(socket);
            return;
        }
        if (!llm_client) {
            // SSE 分支同样保持和非流式一致的模型校验逻辑。
            write_sse_event(socket, "error", build_error_json(kErrorInvalidParameter, "unknown llm id"));
            write_sse_event(socket, "done", R"({})");
            write_sse_end(socket);
            return;
        }

        // 1. 检索知识片段
        auto refs = retriever_->retrieve(query, top_k);
        // 2. 构造 prompt
        std::string prompt = prompt_builder_->build(query, refs);
        // 3. 流式生成回答
        llm_client->stream_generate(
            query,
            refs,
            prompt,
            [&](const std::string &piece) {
                // 每一段回答都通过 token 事件发给客户端
                std::ostringstream oss;
                // 这里拼的是单个 token 事件对应的 JSON 负载，不是完整 SSE 帧。
                oss << R"({"content":")" << escape_json(piece) << "\"}";
                write_sse_event(socket, "token", oss.str());
            });
        // 4. 回答发完后，把引用片段单独发给客户端
        write_sse_event(socket, "refs", build_references_json(refs));
        // 5. 最后发送 done 事件
        write_sse_event(socket, "done", R"({})");
        // 6. 发送结束块
        write_sse_end(socket);
    } catch (const std::exception &ex) {
        log_error("http", std::string("chat stream failed: ") + ex.what());
        try {
            // 即使出错，也尽量给客户端一个 SSE error 事件
            if (!headers_written) {
                write_sse_headers(socket, req.version());
            }
            const auto error = classify_internal_error(ex);
            write_sse_event(socket, "error", build_error_json(error.code, error.message));
            write_sse_event(socket, "done", R"({})");
            write_sse_end(socket);
        } catch (...) {
            // 如果连错误事件都发不出去，就只能静默结束
        }
    }
}

HttpServer::Response HttpServer::handle_remove_document(const Request &req) {
    // 根据请求头获取doc_id
    const std::string doc_id = get_header_value(req, "X-Doc-Id");
    try {
        // 1. 先根据chunk_id删除chunk_embedding
        // 2. 根据doc_id删除chunk
        // 3. 根据doc_id删除document
        ChunkRepository chunk_repo(db_);
        EmbeddingRepository embedding_repo(db_);
        {
            // 获取所有的需要删除的chunk
            auto chunks = chunk_repo.list_by_doc_id(doc_id);
            // 删除chunk
            for (const auto &chunk: chunks) {
                embedding_repo.remove_by_chunk_id(chunk.id);
            }
        }

        // 删除所有chunk
        chunk_repo.remove_by_doc_id(doc_id);

        // 删除 doc
        DocumentRepository doc_repo(db_);
        auto document = doc_repo.find_by_id(doc_id);

        doc_repo.remove_by_id(doc_id);
        // 删除本地存储的文档
        delete_file(document->file_path);
        std::ostringstream oss;
        oss << R"({"status": "success"})";

        return make_json_response(
            req.version(),
            http::status::ok,
            oss.str()
        );
    } catch (const std::exception &ex) {
        log_error("http", std::string("remove document failed: ") + ex.what());
        const auto error = classify_internal_error(ex);
        return make_error_response(
            req.version(),
            http::status::internal_server_error,
            error.code,
            error.message);
    }
}

HttpServer::Response HttpServer::make_error_response(unsigned version,
                                                     boost::beast::http::status status,
                                                     unsigned code,
                                                     const std::string &message) {
    return make_json_response(version, status, build_error_json(code, message));
}
