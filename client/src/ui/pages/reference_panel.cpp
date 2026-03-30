//
// Created by hyp on 2026/3/28.
//

#include "reference_panel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QLabel>
#include <QMenu>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

ReferencePanel::ReferencePanel(QWidget *parent)
    : QWidget(parent) {
    setObjectName("ReferencePanel");
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *title = new QLabel("引用片段", this);
    title->setProperty("role", "sectionTitle");
    layout->addWidget(title);

    referenceView_ = new QWebEngineView(this);
    referenceView_->setObjectName("ReferenceView");
    referenceView_->setStyleSheet("background:#f7f7f7;");
    // 引用区改成自定义右键菜单，只暴露复制动作。
    referenceView_->setContextMenuPolicy(Qt::CustomContextMenu);
    // 允许本地页面访问 CDN，加载 marked 与 KaTeX 资源。
    referenceView_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    referenceView_->page()->setBackgroundColor(QColor("#f7f7f7"));
    referenceViewReady_ = false;
    connect(referenceView_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        showCopyContextMenu(pos);
    });
    connect(referenceView_, &QWebEngineView::loadFinished, this, [this](bool ok) {
        referenceViewReady_ = ok;
        if (ok) {
            renderReferencesJson(pendingRefsJson_);
        }
    });
    // 延后页面加载，让外层 Qt 面板先完成首帧绘制。
    QTimer::singleShot(0, this, [this]() {
        if (referenceView_) {
            referenceView_->setHtml(buildReferencePageHtml(), QUrl("https://qtrag.local/"));
        }
    });

    layout->addWidget(referenceView_, 1);
    // 构造完成后立即渲染空态，占位区域保持稳定。
    clearReferences();
}

void ReferencePanel::setReferences(const QJsonArray &refs) {
    if (!referenceView_) {
        return;
    }
    pendingRefsJson_ = QString::fromUtf8(QJsonDocument(refs).toJson(QJsonDocument::Compact));
    renderReferencesJson(pendingRefsJson_);
}

void ReferencePanel::clearReferences() {
    if (!referenceView_) {
        return;
    }
    pendingRefsJson_ = "[]";
    renderReferencesJson(pendingRefsJson_);
}

void ReferencePanel::showCopyContextMenu(const QPoint &pos) {
    if (!referenceView_) {
        return;
    }

    const bool hasSelection = !referenceView_->page()->selectedText().isEmpty();
    // 只有真的选中了引用文本时才显示复制菜单。
    if (!hasSelection) {
        return;
    }

    QMenu menu(this);
    QAction *copyAction = menu.addAction(QString::fromUtf8("复制"));
    connect(copyAction, &QAction::triggered, this, [this]() {
        if (referenceView_) {
            referenceView_->triggerPageAction(QWebEnginePage::Copy);
        }
    });
    menu.exec(referenceView_->mapToGlobal(pos));
}

QString ReferencePanel::buildReferencePageHtml() const {
    // 页面内统一做：Markdown 解析、表格样式、KaTeX 公式渲染。
    return QString::fromUtf8(R"QTRAG_REF_HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.css">
  <script src="https://cdn.jsdelivr.net/npm/marked@12.0.2/marked.min.js"></script>
  <script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/katex.min.js"></script>
  <script defer src="https://cdn.jsdelivr.net/npm/katex@0.16.11/dist/contrib/auto-render.min.js"></script>
  <style>
    :root {
      --ref-bg: #f7f7f7;
      --card-bg: #ffffff;
      --card-border: #dddddd;
      --muted: #7b7b7b;
      --text: #1f1f1f;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      padding: 2px 2px 8px 2px;
      background: var(--ref-bg);
      color: var(--text);
      font-family: "PingFang SC","Microsoft YaHei UI","Noto Sans CJK SC",sans-serif;
      line-height: 1.5;
      user-select: text;
      -webkit-user-select: text;
    }
    .empty {
      min-height: 180px;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 12px 8px;
    }
    .empty-card {
      width: 100%;
      border: 1px dashed #d5d5d5;
      border-radius: 12px;
      background: #fcfcfc;
      padding: 18px 16px;
      text-align: center;
    }
    .empty-title {
      font-size: 14px;
      font-weight: 700;
      color: #3a3a3a;
      margin-bottom: 6px;
    }
    .empty-desc {
      font-size: 12px;
      color: #8a8a8a;
      line-height: 1.6;
    }
    .ref-card {
      margin: 0 0 10px 0;
      border: 1px solid var(--card-border);
      border-radius: 8px;
      background: var(--card-bg);
      padding: 10px;
      user-select: text;
      -webkit-user-select: text;
    }
    .ref-head {
      display: flex;
      justify-content: space-between;
      gap: 8px;
      font-size: 12px;
      color: var(--muted);
      margin-bottom: 6px;
      flex-wrap: wrap;
    }
    .ref-file {
      color: #3d3d3d;
      font-weight: 700;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: normal;
      overflow-wrap: anywhere;
      word-break: break-word;
    }
    .ref-score { flex: 0 0 auto; }
    .ref-content p { margin: 0 0 8px 0; }
    .ref-content ul, .ref-content ol { margin: 4px 0 8px 18px; }
    .ref-content pre {
      background: #1f2329;
      color: #e8edf2;
      border-radius: 8px;
      padding: 8px;
      overflow: auto;
      margin: 6px 0 10px 0;
    }
    .ref-content code {
      background: #eef2f5;
      border-radius: 4px;
      padding: 1px 4px;
      font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
    }
    .ref-content pre code {
      background: transparent;
      padding: 0;
    }
    .ref-content table {
      border-collapse: collapse;
      margin: 6px 0 10px 0;
      width: 100%;
    }
    .ref-content th, .ref-content td {
      border: 1px solid #d9d9d9;
      padding: 6px 8px;
      text-align: left;
    }
    .ref-content th {
      background: #f6f6f6;
      font-weight: 700;
    }
    .ref-content a {
      color: #1b7f4f;
      text-decoration: none;
    }
    .ref-content,
    .ref-content * {
      min-width: 0;
      overflow-wrap: anywhere;
      word-break: break-word;
    }
  </style>
</head>
<body>
  <div id="refs-root"></div>
  <script>
    window.__pendingReferences = [];

    function escapeHtml(text) {
      return (text || "")
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#39;");
    }

    function markdownToHtml(text) {
      if (window.marked && typeof window.marked.parse === "function") {
        return window.marked.parse(text || "", { gfm: true, breaks: true });
      }
      return "<p>" + escapeHtml(text || "").replace(/\n/g, "<br/>") + "</p>";
    }

    function renderMath(container) {
      if (window.renderMathInElement) {
        window.renderMathInElement(container, {
          delimiters: [
            { left: "$$", right: "$$", display: true },
            { left: "$", right: "$", display: false },
            { left: "\\(", right: "\\)", display: false },
            { left: "\\[", right: "\\]", display: true }
          ],
          throwOnError: false
        });
      }
    }

    function renderEmptyState() {
      return (
        '<div class="empty">' +
          '<div class="empty-card">' +
            '<div class="empty-title">引用片段会显示在这里</div>' +
            '<div class="empty-desc">发起一次问答后，命中的文档片段、分数和 Markdown 内容会在右侧展开。</div>' +
          '</div>' +
        '</div>'
      );
    }

    window.setReferences = function(items) {
      const root = document.getElementById("refs-root");
      if (!root) {
        return;
      }
      window.__pendingReferences = Array.isArray(items) ? items : [];
      if (window.__pendingReferences.length === 0) {
        root.innerHTML = renderEmptyState();
        return;
      }

      root.innerHTML = window.__pendingReferences.map((item) => {
        const filename = escapeHtml(item.filename || "unknown");
        const score = Number(item.score || 0).toFixed(3);
        const content = markdownToHtml(item.text || "");
        return (
          '<div class="ref-card">' +
            '<div class="ref-head">' +
              '<div class="ref-file">' + filename + '</div>' +
              '<div class="ref-score">score ' + score + '</div>' +
            '</div>' +
            '<div class="ref-content">' + content + '</div>' +
          '</div>'
        );
      }).join("");

      renderMath(root);
    };

    window.addEventListener("load", function() {
      window.setReferences(window.__pendingReferences);
    });
</script>
</body>
</html>
)QTRAG_REF_HTML");
}

void ReferencePanel::renderReferencesJson(const QString &json) {
    if (!referenceView_ || !referenceViewReady_) {
        return;
    }
    const QString js = QString("window.setReferences(%1);").arg(json);
    referenceView_->page()->runJavaScript(js);
}
