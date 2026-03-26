//
// Created by hyp on 2026/3/26.
//
#pragma once
#include <QSqlDatabase>
#include <QString>

// 封装客户端本地 SQLite 数据库的打开、关闭和建表逻辑。
class DatabaseManager {
public:
    // dbPath 指向客户端本地 SQLite 数据库文件。
    explicit DatabaseManager(const QString &dbPath);

    // 打开数据库连接；首次调用时会创建命名连接。
    bool open();

    // 主动关闭数据库连接。
    void close();

    // 初始化客户端运行所需的基础表结构。
    bool initializeSchema();

    // 返回当前数据库连接对象，便于上层继续执行查询。
    QSqlDatabase database() const { return db_; }

private:
    // 数据库文件路径。
    QString dbPath_;

    // 当前持有的 Qt SQL 连接对象。
    QSqlDatabase db_;
};
