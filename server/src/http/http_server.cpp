//
// Created by hyp on 2026/3/25.
//

#include "http_server.h"
#include "adapters/embedding_client.h"
#include "adapters/llm_client.h"
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
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cctype>
#include <fstream>
#include <filesystem>
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {
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

    // 把枚举形式的 HTTP 方法转成便于日志输出的字符串。
    std::string method_to_string(http::verb method) {
        return std::string(http::to_string(method));
    }

    // 输出普通运行日志。
    void log_info(const std::string &message) {
        std::cout << "[INFO] " << message << "\n";
    }

    // 输出错误日志。
    void log_error(const std::string &message) {
        std::cerr << "[ERROR] " << message << "\n";
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
    // 只保留字母、数字、点、横杠、下划线
    std::string sanitize_filename(const std::string &filename) {
        std::string result;
        result.reserve(filename.size());
        for (unsigned char ch: filename) {
            if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_') {
                result.push_back(ch);
            }
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

HttpServer::HttpServer(const std::string &address, unsigned short port, sqlite3 *db, std::size_t worker_threads)
    : address_(address),
      port_(port),
      ioc_(1),
      worker_pool_(worker_threads),
      worker_threads_(worker_threads),
      db_(db),
      embedding_client_(std::make_unique<EmbeddingClient>()),
      vector_store_(std::make_unique<InMemoryVectorStore>()),
      retriever_(std::make_unique<Retriever>(
          embedding_client_.get(),
          vector_store_.get(), db_, &db_mutex_)),
      prompt_builder_(std::make_unique<PromptBuilder>()),
      llm_client_(std::make_unique<LLMClient>()) {
}

HttpServer::~HttpServer() = default;


void HttpServer::run() {
    std::ostringstream oss;
    oss << "Starting server on " << address_ << ":" << port_ << "\n";
    log_info(oss.str());

    do_accept_loop();
    worker_pool_.join();
}

void HttpServer::initialize_index_from_storage() {
    // 启动阶段从数据库加载 embedding，恢复内存向量索引
    std::lock_guard<std::mutex> lock(db_mutex_);
    EmbeddingRepository embeddingRepo(db_);
    auto records = embeddingRepo.list_all();
    for (const auto &record: records) {
        vector_store_->add(
            record.chunk_id,
            record.doc_id,
            record.content,
            record.embedding);
    }
    std::ostringstream oss;
    oss << "Restored persisted vector index entries: " << records.size();
    log_info(oss.str());
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
        log_info(oss.str());

        // 流式接口单独处理
        if (req.method() == http::verb::post && req.target() == "/api/v1/chat/stream") {
            handle_chat_stream(socket, req);
            beast::error_code ec;
            socket.shutdown(tcp::socket::shutdown_send, ec);
            return;
        }

        // 其他请求仍走普通分发逻辑
        auto res = handle_request(req);
        // 先把响应完整写回，再主动关闭发送方向。
        http::write(socket, res);
        beast::error_code ec;
        socket.shutdown(tcp::socket::shutdown_send, ec);
    } catch (const std::exception &ex) {
        std::ostringstream oss;
        oss << "Connection handling failed: " << ex.what();
        log_error(oss.str());
    }
}

HttpServer::Response HttpServer::handle_request(const Request &req) {
    // 健康检查接口，便于确认服务是否正常存活。
    if (req.method() == http::verb::get && req.target() == "/health") {
        return make_json_response(
            req.version(),
            http::status::ok,
            R"({"status":"ok"})");
    }

    // 获取文档列表
    if (req.method() == http::verb::get && req.target() == "/api/v1/docs") {
        return handle_list_documents(req);
    }

    // 上传文档
    if (req.method() == http::verb::post && req.target() == "/api/v1/docs/upload") {
        return handle_upload_document(req);
    }

    // 检索
    if (req.method() == http::verb::post && req.target() == "/api/v1/retrieve") {
        return handle_retrieve(req);
    }

    // chat
    if (req.method() == http::verb::post && req.target() == "/api/v1/chat") {
        return handle_chat(req);
    }

    // 对已支持的方法返回 404，说明路径不存在。
    if (req.method() == http::verb::get || req.method() == http::verb::post) {
        return make_json_response(
            req.version(),
            http::status::not_found,
            R"({"error":"not found"})");
    }

    // 其他 HTTP 方法当前未实现。
    return make_json_response(
        req.version(),
        http::status::method_not_allowed,
        R"({"error":"method not allowed"})");
}

HttpServer::Response HttpServer::handle_upload_document(const Request &req) {
    // 读取自定义 header
    const std::string raw_filename = get_header_value(req, "X-Filename");
    const std::string kb_id = get_header_value(req, "X-Kb-Id").empty() ? "default" : get_header_value(req, "X-Kb-Id");
    // 简单校验：文件名不能为空
    if (raw_filename.empty()) {
        return make_json_response(
            req.version(),
            http::status::bad_request,
            R"({"error":"missing X-Filename header"})");
    }
    // 简单校验：请求体不能为空
    if (req.body().empty()) {
        return make_json_response(
            req.version(),
            http::status::bad_request,
            R"({"error":"empty request body"})");
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
        return make_json_response(
            req.version(),
            http::status::internal_server_error,
            R"({"error":"failed to save file"})");
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
        log_error(std::string("upload/index document failed: ") + ex.what());
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
        return make_json_response(
            req.version(),
            http::status::internal_server_error,
            R"({"error":"failed to index document"})");
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
        log_error(std::string("list documents failed: ") + ex.what());
        return make_json_response(
            req.version(),
            http::status::internal_server_error,
            R"({"error":"failed to list documents"})");
    }
}

HttpServer::Response HttpServer::handle_retrieve(const Request &req) {
    // 1. 请求体直接就是 query 文本
    // 2. 可选请求头 X-Top-K 指定返回数量
    const std::string query = req.body();
    const std::size_t top_k = parse_top_k(req, 3);
    if (query.empty()) {
        return make_json_response(
            req.version(),
            http::status::bad_request,
            R"({"error":"empty query"})");
    }
    try {
        // 调用 retriever 做索引
        auto items = retriever_->retrieve(query, top_k);
        return make_json_response(
            req.version(),
            http::status::ok,
            build_retrieve_result_json(items));
    } catch (const std::exception &ex) {
        log_error(std::string("retrieve failed: ") + ex.what());
        return make_json_response(
            req.version(),
            http::status::internal_server_error,
            R"({"error":"retrieve failed"})");
    }
}

HttpServer::Response HttpServer::handle_chat(const Request &req) {
    // 1. 请求体直接放 query 文本
    // 2. X-Top-K 指定检索数量，默认 3
    const std::string query = req.body();
    const std::size_t top_k = parse_top_k(req, 3);
    // 空 query 直接返回 400
    if (query.empty()) {
        return make_json_response(
            req.version(),
            http::status::bad_request,
            R"({"error":"empty query"})");
    }
    try {
        // 1. 检索知识库中的相关片段
        auto refs = retriever_->retrieve(query, top_k);
        // 2. 构造 prompt
        std::string prompt = prompt_builder_->build(query, refs);
        // 3. 调用LLM生成回答
        std::string answer = llm_client_->generate(query, refs, prompt);
        // 4. 返回 answer + reference
        return make_json_response(
            req.version(),
            http::status::ok,
            build_chat_result_json(answer, refs));
    } catch (const std::exception &ex) {
        log_error(std::string("chat request failed: ") + ex.what());
        return make_json_response(
            req.version(),
            http::status::internal_server_error,
            R"({"error":"chat failed"})");
    }
}

void HttpServer::handle_chat_stream(boost::asio::ip::tcp::socket &socket,
                                    const Request &req) {
    // 1. 请求体直接就是 query 文本
    // 2. X-Top-K 指定检索数量，默认 3
    const std::string query = req.body();
    const std::size_t top_k = parse_top_k(req, 3);
    try {
        // 先写 SSE 响应头
        write_sse_headers(socket, req.version());
        // query 为空时，直接发 error + done
        if (query.empty()) {
            write_sse_event(socket, "error", R"({"message":"empty query"})");
            write_sse_event(socket, "done", R"({})");
            write_sse_end(socket);
            return;
        }

        // 1. 检索知识片段
        auto refs = retriever_->retrieve(query, top_k);
        // 2. 构造 prompt
        std::string prompt = prompt_builder_->build(query, refs);
        // 3. 流式生成回答
        llm_client_->stream_generate(
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
        log_error(std::string("chat stream failed: ") + ex.what());
        try {
            // 即使出错，也尽量给客户端一个 SSE error 事件
            write_sse_headers(socket, req.version());
            std::ostringstream oss;
            oss << R"({"message":")" << escape_json(ex.what()) << "\"}";
            write_sse_event(socket, "error", oss.str());
            write_sse_event(socket, "done", R"({})");
            write_sse_end(socket);
        } catch (...) {
            // 如果连错误事件都发不出去，就只能静默结束
        }
    }
}
