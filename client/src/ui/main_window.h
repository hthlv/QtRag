//
// Created by hyp on 2026/3/25.
//

#pragma once
#include <QMainWindow>
class QListWidget;
class QTextEdit;
class QPushButton;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void setupUi();
    void setupMenu();

private:
    QListWidget *leftList_;
    QTextEdit *chatView_;
    QTextEdit *inputEdit_;
    QPushButton *sendButton_;
    QListWidget *referenceList_;

    bool is_connected_{false};
};
