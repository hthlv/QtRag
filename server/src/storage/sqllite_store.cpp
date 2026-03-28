//
// Created by hyp on 2026/3/26.
//

#include "sqllite_store.h"
#include "utils/logger.h"
#include <stdexcept>

SqlliteStore::SqlliteStore(const std::string &db_path)
    :db_path_(db_path){
}

SqlliteStore::~SqlliteStore() {
    // 保证对象生命周期结束时数据库连接被正确释放。
    close();
}

void SqlliteStore::open() {
    if (db_) {
        return;
    }

    // sqlite3_open 成功后会把句柄写入 db_。
    int rc = sqlite3_open(db_path_.c_str(), &db_);

    // 打开失败时需要立即回收句柄并抛出异常，避免留下半初始化状态。
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open sqlite db: " + err);
    }
    log_info("db", "Opened database: " + db_path_);
}

void SqlliteStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        log_info("db", "Closed database");
    }
}

void SqlliteStore::execute(const std::string &sql) {
    // 这里走 sqlite3_exec，适合启动期建表这类简单语句。
    char *err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown sqlite error";
        sqlite3_free(err_msg);
        log_error("db", err);
        throw std::runtime_error("SQLite exec failed: " + err);
    }
}

void SqlliteStore::initialize_schema() {
    // documents 表保存原始文档元信息和分块状态。
    const std::string create_documents_sql = R"(
        CREATE TABLE IF NOT EXISTS documents (
            id TEXT PRIMARY KEY,
            kb_id TEXT NOT NULL DEFAULT 'default',
            filename TEXT NOT NULL,
            file_path TEXT NOT NULL,
            status TEXT NOT NULL,
            chunk_count INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        );
    )";

    // chunks 表保存分块后的文本内容，后续可继续扩展 embedding 字段。
    const std::string create_chunks_sql = R"(
        CREATE TABLE IF NOT EXISTS chunks (
            id TEXT PRIMARY KEY,
            doc_id TEXT NOT NULL,
            chunk_index INTEGER NOT NULL,
            content TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            FOREIGN KEY(doc_id) REFERENCES documents(id)
        );
    )";

    const std::string create_chunk_embeddings_sql = R"(
        CREATE TABLE IF NOT EXISTS chunk_embeddings (
            chunk_id TEXT PRIMARY KEY,
            doc_id TEXT NOT NULL,
            content TEXT NOT NULL,
            embedding TEXT NOT NULL,
            created_at INTEGER NOT NULL
        );
    )";

    execute(create_documents_sql);
    execute(create_chunks_sql);
    execute(create_chunk_embeddings_sql);

    // 只要两张核心表就绪，后端原型就能接受健康检查和后续文档接入。
    log_info("db", "Initialized schema");
}
