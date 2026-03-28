//
// Created by hyp on 2026/3/27.
//

#include "settings_dialog.h"
#include "ui/notifier.h"
#include "storage/repositories/settings_repository.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(SettingsRepository *settingsRepo, QWidget *parent)
    : QDialog(parent),
      settingsRepo_(settingsRepo) {
    setObjectName("SettingsDialog");
    setupUi();
    loadFromLocal();
    setWindowTitle("设置");
    resize(500, 140);
}

void SettingsDialog::setupUi() {
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 20, 20, 20);
    rootLayout->setSpacing(16);
    auto *formLayout = new QFormLayout();
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(12);

    // 服务端地址决定所有请求会发往哪个后端实例。
    serverUrlEdit_ = new QLineEdit(this);
    serverUrlEdit_->setPlaceholderText("http://127.0.0.1:8080");
    formLayout->addRow("服务端地址：", serverUrlEdit_);

    // Top-K 控制每次检索会带回多少条引用片段。
    topkkSpinBox_ = new QSpinBox(this);
    topkkSpinBox_->setRange(1, 20);
    topkkSpinBox_->setValue(3);
    topkkSpinBox_->setToolTip("聊天与检索都会使用这个 Top-K");
    formLayout->addRow("检索 Top-K：", topkkSpinBox_);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(10);
    buttonLayout->addStretch();
    saveButton_ = new QPushButton("保存", this);
    saveButton_->setProperty("variant", "primary");
    cancelButton_ = new QPushButton("取消", this);
    cancelButton_->setProperty("variant", "ghost");
    buttonLayout->addWidget(saveButton_);
    buttonLayout->addWidget(cancelButton_);
    rootLayout->addLayout(formLayout);
    rootLayout->addLayout(buttonLayout);
    connect(saveButton_, &QPushButton::clicked, this, [this]() {
        saveToLocal();
    });
    connect(cancelButton_, &QPushButton::clicked, this, [this]() {
        reject();
    });
}

void SettingsDialog::loadFromLocal() {
    if (!settingsRepo_) {
        return;
    }
    auto value = settingsRepo_->getValue("server_url");
    if (value.has_value()) {
        serverUrlEdit_->setText(*value);
    } else {
        serverUrlEdit_->setText("http://127.0.0.1:8080");
    }

    // 没有本地配置时回退到默认 Top-K=3。
    auto topkValue = settingsRepo_->getValue("top_k");
    bool ok = false;
    const int topk = topkValue.has_value() ? topkValue->toInt(&ok) : 3;
    topkkSpinBox_->setValue(ok ? topk : 3);
}

void SettingsDialog::saveToLocal() {
    if (!settingsRepo_) {
        UiNotifier::warning(this, "错误", "设置仓库未初始化", true);
        return;
    }
    const QString serverUrl = serverUrlEdit_->text().trimmed();
    if (serverUrl.isEmpty()) {
        UiNotifier::warning(this, "提示", "服务端地址不能为空");
        return;
    }
    if (!settingsRepo_->setValue("server_url", serverUrl)) {
        UiNotifier::warning(this, "错误", "保存设置失败", true);
        return;
    }

    // Top-K 单独持久化，供主窗口请求服务端时复用。
    if (!settingsRepo_->setValue("top_k", QString::number(topkkSpinBox_->value()))) {
        UiNotifier::warning(this, "错误", "保存 Top-K 失败", true);
        return;
    }
    UiNotifier::info(this, "设置已保存");
    accept();
}
