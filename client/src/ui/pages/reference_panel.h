//
// Created by hyp on 2026/3/28.
//

#pragma once
#include <QWidget>
#include <QPoint>
class QWebEngineView;
class QJsonArray;

class ReferencePanel : public QWidget {
    Q_OBJECT

public:
    explicit ReferencePanel(QWidget *parent = nullptr);

    void setReferences(const QJsonArray &refs);

    void clearReferences();

    // 引用区右键菜单只保留复制。
    void showCopyContextMenu(const QPoint &pos);

private:
    QString buildReferencePageHtml() const;

    void renderReferencesJson(const QString &json);

    QWebEngineView *referenceView_{nullptr};
    bool referenceViewReady_{false};
    QString pendingRefsJson_{"[]"};
};
