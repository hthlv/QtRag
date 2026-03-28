//
// Created by hyp on 2026/3/28.
//

#include "notifier.h"

#include <QApplication>
#include <QMainWindow>
#include <QMessageBox>
#include <QStatusBar>
#include <QWidget>

namespace {
// 优先从当前页面向上查找主窗口状态栏；找不到时回退到活动窗口。
QStatusBar *resolve_status_bar(QWidget *context) {
    QWidget *cursor = context;
    while (cursor) {
        if (auto *mw = qobject_cast<QMainWindow *>(cursor)) {
            return mw->statusBar();
        }
        cursor = cursor->parentWidget();
    }

    if (auto *activeWindow = QApplication::activeWindow()) {
        if (auto *mw = qobject_cast<QMainWindow *>(activeWindow)) {
            return mw->statusBar();
        }
    }
    return nullptr;
}

void post_status(QWidget *context, const QString &message, int timeoutMs) {
    if (message.trimmed().isEmpty()) {
        return;
    }
    if (auto *statusBar = resolve_status_bar(context)) {
        statusBar->showMessage(message, timeoutMs);
    }
}
}

void UiNotifier::info(QWidget *context, const QString &message, int timeoutMs) {
    post_status(context, message, timeoutMs);
}

void UiNotifier::warning(QWidget *context,
                         const QString &title,
                         const QString &message,
                         bool popup,
                         int timeoutMs) {
    // 警告默认只提示状态栏，按调用方策略决定是否弹窗。
    post_status(context, message, timeoutMs);
    if (popup) {
        QMessageBox::warning(context, title, message);
    }
}

void UiNotifier::error(QWidget *context,
                       const QString &title,
                       const QString &message,
                       bool popup,
                       int timeoutMs) {
    // 错误默认走弹窗，同时在状态栏保留一条短提示。
    post_status(context, message, timeoutMs);
    if (popup) {
        QMessageBox::critical(context, title, message);
    }
}
