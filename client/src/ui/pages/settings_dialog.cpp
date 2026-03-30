//
// Created by hyp on 2026/3/27.
//

#include "settings_dialog.h"
#include "ui/notifier.h"
#include "storage/repositories/settings_repository.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(SettingsRepository *settingsRepo, QWidget *parent)
    : QDialog(parent),
      settingsRepo_(settingsRepo),
      networkManager_(new QNetworkAccessManager(this)) {
    setObjectName("SettingsDialog");
    setupUi();
    loadFromLocal();
    setWindowTitle("设置");
    resize(560, 220);
    fetchAvailableModels();
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
    serverUrlEdit_->setTextMargins(12, 10, 12, 10);
    formLayout->addRow("服务端地址：", serverUrlEdit_);

    // Top-K 控制每次检索会带回多少条引用片段。
    topkkSpinBox_ = new QSpinBox(this);
    topkkSpinBox_->setRange(1, 20);
    topkkSpinBox_->setValue(3);
    topkkSpinBox_->setToolTip("聊天与检索都会使用这个 Top-K");
    formLayout->addRow("检索 Top-K：", topkkSpinBox_);

    llmComboBox_ = new QComboBox(this);
    llmComboBox_->setEditable(false);
    llmComboBox_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    // 第一项固定为空值，表示跟随服务端默认模型。
    llmComboBox_->addItem("跟随服务端默认模型", "");
    refreshModelsButton_ = new QPushButton("刷新模型", this);
    refreshModelsButton_->setProperty("variant", "ghost");
    auto *llmRowLayout = new QHBoxLayout();
    llmRowLayout->setSpacing(10);
    llmRowLayout->addWidget(llmComboBox_, 1);
    llmRowLayout->addWidget(refreshModelsButton_);
    formLayout->addRow("聊天模型：", llmRowLayout);

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
    connect(refreshModelsButton_, &QPushButton::clicked, this, [this]() {
        fetchAvailableModels();
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

    const QString selectedLlmId = settingsRepo_->getValue("selected_llm_id").value_or(QString()).trimmed();
    const int selectedIndex = llmComboBox_->findData(selectedLlmId);
    llmComboBox_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
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
    if (!settingsRepo_->setValue("selected_llm_id", llmComboBox_->currentData().toString().trimmed())) {
        UiNotifier::warning(this, "错误", "保存模型选择失败", true);
        return;
    }
    UiNotifier::info(this, "设置已保存");
    accept();
}

void SettingsDialog::fetchAvailableModels() {
    const QString serverUrl = serverUrlEdit_->text().trimmed();
    if (serverUrl.isEmpty()) {
        UiNotifier::warning(this, "提示", "请先填写服务端地址");
        return;
    }

    refreshModelsButton_->setEnabled(false);
    UiNotifier::info(this, "正在拉取模型列表...");
    // 模型列表由服务端统一提供，客户端不做本地硬编码。
    QNetworkRequest request(QUrl(serverUrl + "/api/v1/models"));
    QNetworkReply *reply = networkManager_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        refreshModelsButton_->setEnabled(true);
        const QByteArray responseData = reply->readAll();
        const QString errorText = reply->errorString();
        const bool hasError = reply->error() != QNetworkReply::NoError;
        reply->deleteLater();
        if (hasError) {
            UiNotifier::warning(this, "提示", "拉取模型列表失败：" + errorText);
            return;
        }
        applyModelsFromJson(responseData);
        UiNotifier::info(this, "模型列表已刷新");
    });
}

void SettingsDialog::applyModelsFromJson(const QByteArray &jsonData) {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        UiNotifier::warning(this, "错误", "模型列表返回格式不正确", true);
        return;
    }

    const QString persistedSelection = settingsRepo_
                                           ? settingsRepo_->getValue("selected_llm_id").value_or(QString()).trimmed()
                                           : QString();
    const QJsonObject root = doc.object();
    const QString defaultLlmId = root.value("default_llm_id").toString().trimmed();
    const QJsonArray items = root.value("items").toArray();

    // 每次刷新都重建下拉项，保证与服务端可用模型同步。
    llmComboBox_->clear();
    llmComboBox_->addItem("跟随服务端默认模型", "");
    for (const auto &itemValue : items) {
        const QJsonObject item = itemValue.toObject();
        const QString id = item.value("id").toString().trimmed();
        const QString label = item.value("label").toString().trimmed();
        const QString model = item.value("model").toString().trimmed();
        const QString providerType = item.value("provider_type").toString().trimmed();
        QString displayText = label.isEmpty() ? id : label;
        if (!model.isEmpty()) {
            displayText += " (" + model + ")";
        }
        if (!providerType.isEmpty()) {
            displayText += " [" + providerType + "]";
        }
        llmComboBox_->addItem(displayText, id);
    }

    QString targetSelection = persistedSelection;
    if (targetSelection.isEmpty()) {
        // 本地未保存时，优先落到服务端返回的默认模型。
        targetSelection = defaultLlmId;
    }
    const int selectedIndex = llmComboBox_->findData(targetSelection);
    llmComboBox_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : 0);
}
