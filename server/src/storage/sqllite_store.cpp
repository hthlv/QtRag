//
// Created by hyp on 2026/3/26.
//

#include "sqllite_store.h"
#include <iostream>
#include <stdexcept>

namespace {
    void log_info(const std::string &msg) {
        std::cout << "[DB][INFO] " << msg << "\n";
    }

    void log_error(const std::string &msg) {
        std::cerr << "[DB][ERROR] " << msg << "\n";
    }
}

SqlliteStore::SqlliteStore(const std::string &db_path)
    :db_path_(db_path){
}

SqlliteStore::~SqlliteStore() {
    close();
}

void SqlliteStore::open() {
    if (db_) {
        return;
    }
    //
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    //
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open sqlite db: " + err);
    }
    log_info("Opened database: "  + db_path_);
}

void SqlliteStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
        log_info("Closed database");
    }
}

void SqlliteStore::execute(const std::string &sql) {
    char *err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "unknown sqlite error";
        sqlite3_free(err_msg);
        throw std::runtime_error("SQLite exec failed: " + err);
    }
}

void SqlliteStore::initialize_schema() {
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
    execute(create_documents_sql);
    execute(create_chunks_sql);
    log_info("Initialized schema");
}