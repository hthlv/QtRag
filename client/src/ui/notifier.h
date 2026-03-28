//
// Created by hyp on 2026/3/28.
//

#pragma once

#include <QString>

class QWidget;

// UI 提示分级器：普通信息默认走状态栏，严重问题再弹窗。
class UiNotifier {
public:
    static void info(QWidget *context, const QString &message, int timeoutMs = 5000);

    static void warning(QWidget *context,
                        const QString &title,
                        const QString &message,
                        bool popup = false,
                        int timeoutMs = 7000);

    static void error(QWidget *context,
                      const QString &title,
                      const QString &message,
                      bool popup = true,
                      int timeoutMs = 9000);
};
