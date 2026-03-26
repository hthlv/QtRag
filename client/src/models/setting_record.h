//
// Created by hyp on 2026/3/26.
//

#pragma once
#include <QString>

// 对应 settings 表的一条配置项记录。
struct SettingRecord {
    // 配置键。
    QString key;

    // 配置值。
    QString value;
};
