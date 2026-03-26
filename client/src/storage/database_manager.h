//
// Created by hyp on 2026/3/26.
//
#pragma once
#include <QSqlDatabase>
#include <QString>

class DatabaseManager {
public:
    explicit DatabaseManager(const QString &dbPath);

    bool open();

    void close();

    bool initializeSchema();

    QSqlDatabase database() const { return db_; }

private:
    QString dbPath_;
    QSqlDatabase db_;
};
