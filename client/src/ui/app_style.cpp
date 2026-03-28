//
// Created by hyp on 2026/3/28.
//

#include "app_style.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QStringList>

namespace {
QFont chooseUiFont() {
    const QStringList preferredFamilies = {
        "SF Pro Display",
        "PingFang SC",
        "Microsoft YaHei UI",
        "Noto Sans CJK SC",
        "Source Han Sans SC",
        "WenQuanYi Micro Hei"
    };

    const QFontDatabase database;
    for (const QString &family : preferredFamilies) {
        if (database.families().contains(family)) {
            QFont font(family);
            font.setPointSize(10);
            return font;
        }
    }

    QFont font;
    font.setPointSize(10);
    return font;
}

QString buildAppStyleSheet() {
    return QString::fromUtf8(R"(
QWidget {
    color: #1f1f1f;
    background: #ededed;
    selection-background-color: #07c160;
    selection-color: #ffffff;
}

QMainWindow,
QDialog {
    background: #ededed;
}

QMainWindow::separator {
    width: 1px;
    height: 1px;
    background: #dddddd;
}

QMenuBar {
    background: #f7f7f7;
    color: #262626;
    border-bottom: 1px solid #dddddd;
    padding: 6px 10px;
    spacing: 8px;
}

QMenuBar::item {
    background: transparent;
    border-radius: 10px;
    padding: 7px 12px;
}

QMenuBar::item:selected {
    background: #e9e9e9;
}

QMenu {
    background: #ffffff;
    border: 1px solid #d9d9d9;
    padding: 8px;
}

QMenu::item {
    padding: 8px 20px;
    border-radius: 8px;
}

QMenu::item:selected {
    background: #efefef;
}

QStatusBar {
    background: #f7f7f7;
    color: #666666;
    border-top: 1px solid #dddddd;
}

QStatusBar::item {
    border: none;
}

QLabel {
    color: #2a2a2a;
    background: transparent;
}

QLabel[role="sectionTitle"] {
    color: #222222;
    font-size: 16px;
    font-weight: 700;
    padding-bottom: 6px;
}

QWidget#MainSurface,
QWidget#PageSurface,
QWidget#DialogSurface {
    background: #ededed;
}

QWidget#LeftPanel,
QWidget#RightPanel,
QWidget#ReferencePanel,
QDialog#DocumentPage,
QDialog#SettingsDialog {
    background: #f7f7f7;
    border: 1px solid #dddddd;
    border-radius: 14px;
}

QWidget#CenterPanel {
    background: #ededed;
    border: 1px solid #dddddd;
    border-radius: 14px;
}

QTextEdit,
QLineEdit,
QListWidget {
    background: #ffffff;
    color: #1f1f1f;
    border: 1px solid #d9d9d9;
    border-radius: 10px;
    padding: 10px 12px;
}

QTableWidget,
QTextBrowser,
QWebEngineView {
    background: #ffffff;
    color: #1f1f1f;
    border: 1px solid #d9d9d9;
    border-radius: 10px;
}

QTextEdit:focus,
QLineEdit:focus,
QListWidget:focus,
QTableWidget:focus {
    border: 1px solid #07c160;
}

QListWidget#SessionList {
    background: #ffffff;
    border-radius: 10px;
    padding: 8px;
    outline: none;
}

QListWidget#SessionList::item {
    border-radius: 8px;
    padding: 10px 12px;
    margin: 4px 0;
}

QListWidget#SessionList::item:selected {
    background: #d8f8e5;
    color: #0c6b42;
}

QListWidget#SessionList::item:hover:!selected {
    background: #f3f3f3;
}

QTextEdit#ChatView,
QWebEngineView#ChatView {
    background: #ededed;
    border: none;
    border-radius: 10px;
}

QTextEdit#InputEdit {
    background: #ffffff;
    border-radius: 8px;
    padding: 14px 16px;
}

QTextBrowser#ReferenceView,
QWebEngineView#ReferenceView {
    background: #f7f7f7;
    border: none;
    border-radius: 8px;
}

QPushButton {
    background: #f4f4f4;
    color: #1f1f1f;
    border: 1px solid #d9d9d9;
    border-radius: 8px;
    padding: 8px 16px;
    font-weight: 600;
}

QPushButton:hover {
    background: #ebebeb;
}

QPushButton:pressed {
    background: #e3e3e3;
}

QPushButton[variant="primary"] {
    background: #07c160;
    color: #ffffff;
    border: 1px solid #06ad56;
}

QPushButton[variant="primary"]:hover {
    background: #06b358;
}

QPushButton[variant="primary"]:pressed {
    background: #05a450;
}

QPushButton[variant="ghost"] {
    background: #f5f5f5;
}

QHeaderView::section {
    background: #f5f5f5;
    color: #555555;
    border: none;
    border-bottom: 1px solid #e4e4e4;
    padding: 10px 12px;
    font-weight: 700;
}

QTableWidget {
    gridline-color: #ececec;
    alternate-background-color: #fafafa;
}

QTableWidget::item {
    padding: 8px;
}

QScrollBar:vertical {
    background: transparent;
    width: 12px;
    margin: 8px 4px 8px 0;
}

QScrollBar::handle:vertical {
    background: #cfcfcf;
    border-radius: 6px;
    min-height: 32px;
}

QScrollBar::handle:vertical:hover {
    background: #bdbdbd;
}

QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical,
QScrollBar:horizontal,
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
    background: transparent;
    border: none;
    width: 0;
    height: 0;
}

QSplitter::handle {
    background: transparent;
}

QSplitter::handle:horizontal {
    width: 14px;
    margin: 18px 0;
}

QSplitter::handle:horizontal:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                stop:0 transparent,
                                stop:0.48 #d8d8d8,
                                stop:0.52 #d8d8d8,
                                stop:1 transparent);
}

QMessageBox {
    background: #ffffff;
}
)");
}
}

void applyAppStyle(QApplication *app) {
    if (!app) {
        return;
    }

    app->setFont(chooseUiFont());

    QPalette palette = app->palette();
    palette.setColor(QPalette::Window, QColor("#ededed"));
    palette.setColor(QPalette::Base, QColor("#ffffff"));
    palette.setColor(QPalette::AlternateBase, QColor("#fafafa"));
    palette.setColor(QPalette::Text, QColor("#1f1f1f"));
    palette.setColor(QPalette::Button, QColor("#f4f4f4"));
    palette.setColor(QPalette::ButtonText, QColor("#1f1f1f"));
    palette.setColor(QPalette::Highlight, QColor("#07c160"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    app->setPalette(palette);

    app->setStyleSheet(buildAppStyleSheet());
}
