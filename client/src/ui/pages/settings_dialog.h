//
// Created by hyp on 2026/3/27.
//

#pragma once
#include <QDialog>
class QLineEdit;
class QPushButton;
class SettingsRepository;

// 设置页：当前先支持配置服务端地址
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(SettingsRepository *settingsRepo, QWidget *parent = nullptr);
private:
    void setupUi();
    void loadFromLocal();
    void saveToLocal();
private:
    SettingsRepository *settingsRepo_{nullptr};
    QLineEdit *serverUrlEdit_{nullptr};
    QPushButton *saveButton_{nullptr};
    QPushButton *cancelButton_{nullptr};
};
