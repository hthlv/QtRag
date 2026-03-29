//
// Created by hyp on 2026/3/27.
//

#pragma once
#include <QDialog>
class QComboBox;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class SettingsRepository;
class QSpinBox;

// 设置页：当前先支持配置服务端地址
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsRepository *settingsRepo, QWidget *parent = nullptr);
private:
    void setupUi();
    void loadFromLocal();
    void saveToLocal();
    void fetchAvailableModels();
    void applyModelsFromJson(const QByteArray &jsonData);
private:
    SettingsRepository *settingsRepo_{nullptr};
    QNetworkAccessManager *networkManager_{nullptr};
    QLineEdit *serverUrlEdit_{nullptr};
    // Top-K 设置
    QSpinBox *topkkSpinBox_{nullptr};
    QComboBox *llmComboBox_{nullptr};
    QPushButton *refreshModelsButton_{nullptr};
    QPushButton *saveButton_{nullptr};
    QPushButton *cancelButton_{nullptr};
};
