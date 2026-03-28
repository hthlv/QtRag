//
// Created by hyp on 2026/3/28.
//

#include "reference_panel.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>


ReferencePanel::ReferencePanel(QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel("引用片段", this));

    referenceList_ = new QListWidget(this);
    referenceList_->setSelectionMode(QAbstractItemView::NoSelection);
    referenceList_->setWordWrap(true);
    layout->addWidget(referenceList_, 1);
}

void ReferencePanel::setReferences(const QJsonArray &refs) {
    if (!referenceList_) {
        return;
    }
    referenceList_->clear();
    if (refs.isEmpty()) {
        referenceList_->addItem("无引用片段");
        return;
    }
    for (const auto &ref: refs) {
        QJsonObject obj = ref.toObject();
        QString filename = obj.value("filename").toString();
        double score = obj.value("score").toDouble();
        QString text = obj.value("text").toString();
        QString itemText = QString("[%1] score=%2\n%3")
                .arg(filename.isEmpty() ? "unknown" : filename)
                .arg(score, 0, 'f', 3)
                .arg(text.left(120));
        referenceList_->addItem(itemText);
    }
}

void ReferencePanel::clearReferences() {
    if (!referenceList_) {
        return;
    }
    referenceList_->clear();
}
