//
// Created by hyp on 2026/3/28.
//

#pragma once
#include <QWidget>
class QListWidget;
class QJsonArray;

class ReferencePanel : public QWidget {
    Q_OBJECT

public:
    explicit ReferencePanel(QWidget *parent = nullptr);

    void setReferences(const QJsonArray &refs);

    void clearReferences();

private:
    QListWidget *referenceList_{nullptr};
};
