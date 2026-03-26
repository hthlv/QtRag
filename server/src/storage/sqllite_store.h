//
// Created by hyp on 2026/3/26.
//
#pragma once
#include <sqlite3.h>
#include <string>

// 封装服务端 SQLite 连接和基础 SQL 执行逻辑。
class SqlliteStore {
public:
    // db_path 是服务端数据库文件的落盘位置。
    explicit SqlliteStore(const std::string &db_path);

    // 析构时自动关闭数据库，避免连接泄漏。
    ~SqlliteStore();

    // 打开数据库连接；重复调用时会直接复用已有连接。
    void open();

    // 关闭数据库连接并释放 sqlite3 句柄。
    void close();

    // 初始化服务端运行所需的表结构。
    void initialize_schema();

    // 执行一条不需要返回结果集的 SQL 语句。
    void execute(const std::string &sql);

    // 暴露底层 sqlite3 句柄，便于后续扩展查询接口。
    sqlite3 *db() const { return this->db_; }

private:
    // 数据库文件路径。
    std::string db_path_;

    // 原生 sqlite3 连接句柄。
    sqlite3 *db_{nullptr};
};
