//
// Created by hyp on 2026/3/28.
//

#pragma once
#include <QWidget>
#ifdef QTRAG_CLIENT_HAS_WEBENGINE
class QWebEngineView;
#else
class QTextBrowser;
#endif
class QJsonArray;

class ReferencePanel : public QWidget {
    Q_OBJECT

public:
    explicit ReferencePanel(QWidget *parent = nullptr);

    void setReferences(const QJsonArray &refs);

    void clearReferences();

private:
#ifdef QTRAG_CLIENT_HAS_WEBENGINE
    QString buildReferencePageHtml() const;

    void renderReferencesJson(const QString &json);

    QWebEngineView *referenceView_{nullptr};
    bool referenceViewReady_{false};
    QString pendingRefsJson_{"[]"};
#else
    QString markdownToHtmlFragment(const QString &markdown) const;

    QTextBrowser *referenceView_{nullptr};
#endif
};
