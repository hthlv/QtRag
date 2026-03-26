//
// Created by hyp on 2026/3/26.
//
#pragma once
#include <sqlite3.h>
#include <string>

class SqlliteStore {
public:
    explicit SqlliteStore(const std::string &db_path);

    ~SqlliteStore();

    void open();

    void close();
    // 初始化表
    void initialize_schema();
    // 执行sql语句
    void execute(const std::string &sql);

    sqlite3 *db() const { return this->db_; }

private:
    std::string db_path_;
    sqlite3 *db_{nullptr};
};
