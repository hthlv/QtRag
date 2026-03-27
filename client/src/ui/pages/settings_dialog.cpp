//
// Created by hyp on 2026/3/27.
//

#include "settings_dialog.h"
#include "storage/repositories/settings_repository.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(SettingsRepository *settingsRepo, QWidget *parent)
    : QDialog(parent),
      settingsRepo_(settingsRepo) {
    setupUi();
    loadFromLocal();
    setWindowTitle("设置");
    resize(500, 140);
}

void SettingsDialog::setupUi() {
    auto *rootLayout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout();
    serverUrlEdit_ = new QLineEdit(this);
    formLayout->addRow("服务端地址：", serverUrlEdit_);
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    saveButton_ = new QPushButton("保存", this);
    cancelButton_ = new QPushButton("取消", this);
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
}

void SettingsDialog::saveToLocal() {
    if (!settingsRepo_) {
        QMessageBox::warning(this, "错误", "设置仓库未初始化");
        return;
    }
    const QString serverUrl = serverUrlEdit_->text().trimmed();
    if (serverUrl.isEmpty()) {
        QMessageBox::warning(this, "错误", "服务端地址不能为空");
        return;
    }
    if (!settingsRepo_->setValue("server_url", serverUrl)) {
        QMessageBox::warning(this, "错误", "保存设置失败");
        return;
    }
    accept();
}