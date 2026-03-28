//
// Created by hyp on 2026/3/28.
//

#include "reference_panel.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QVBoxLayout>
#ifdef QTRAG_CLIENT_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#else
#include <QFrame>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QTextDocument>
#include <QtGlobal>
#endif

ReferencePanel::ReferencePanel(QWidget *parent)
    : QWidget(parent) {
    setObjectName("ReferencePanel");
    setMinimumWidth(280);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *title = new QLabel("引用片段", this);
    title->setProperty("role", "sectionTitle");
    layout->addWidget(title);

#ifdef QTRAG_CLIENT_HAS_WEBENGINE
    referenceView_ = new QWebEngineView(this);
    referenceView_->setObjectName("ReferenceView");
    // 引用区同样关闭默认右键菜单，只保留业务展示能力。
    referenceView_->setContextMenuPolicy(Qt::NoContextMenu);
    // 允许本地页面访问 CDN，加载 marked 与 KaTeX 资源。
    referenceView_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    referenceViewReady_ = false;
    connect(referenceView_, &QWebEngineView::loadFinished, this, [this](bool ok) {
        referenceViewReady_ = ok;
        if (ok) {
            renderReferencesJson(pendingRefsJson_);
        }
    });
    // 先加载固定页面骨架，后续只推送 JSON 数据。
    referenceView_->setHtml(buildReferencePageHtml(), QUrl("https://qtrag.local/"));
#else
    // 回退模式：系统未安装 WebEngine 时，仍然使用 QTextBrowser 展示引用。
    referenceView_ = new QTextBrowser(this);
    referenceView_->setObjectName("ReferenceView");
    referenceView_->setReadOnly(true);
    referenceView_->setOpenExternalLinks(true);
    referenceView_->setFrameShape(QFrame::NoFrame);
    referenceView_->document()->setDefaultStyleSheet(QString::fromUtf8(R"(
p { margin: 0 0 8px 0; }
ul, ol { margin: 4px 0 8px 18px; }
table.md-table {
    border-collapse: collapse;
    margin: 6px 0 10px 0;
    width: 100%;
}
table.md-table th,
table.md-table td {
    border: 1px solid #d9d9d9;
    padding: 6px 8px;
    text-align: left;
}
table.md-table th {
    background: #f6f6f6;
    font-weight: 700;
}
pre {
    background: #1f2329;
    color: #e8edf2;
    border-radius: 8px;
    padding: 8px;
}
code {
    background: #eef2f5;
    border-radius: 4px;
    padding: 1px 4px;
    font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
}
.math-inline {
    background: #eef2f5;
    border-radius: 4px;
    padding: 1px 6px;
    font-family: "Cambria Math", "Times New Roman", serif;
}
.math-block {
    margin: 6px 0 10px 0;
    padding: 8px 10px;
    background: #f5f7f8;
    border-left: 3px solid #07c160;
    border-radius: 6px;
    white-space: pre-wrap;
    font-family: "Cambria Math", "Times New Roman", serif;
}
a { color: #1b7f4f; text-decoration: none; }
)"));
#endif

    layout->addWidget(referenceView_, 1);
    // 构造完成后立即渲染空态，占位区域保持稳定。
    clearReferences();
}

void ReferencePanel::setReferences(const QJsonArray &refs) {
    if (!referenceView_) {
        return;
    }
#ifdef QTRAG_CLIENT_HAS_WEBENGINE
    pendingRefsJson_ = QString::fromUtf8(QJsonDocument(refs).toJson(QJsonDocument::Compact));
    renderReferencesJson(pendingRefsJson_);
#else
    QString html;
    html += "<div style='padding: 2px 0 6px 0;'>";
    if (refs.isEmpty()) {
        html += QString::fromUtf8(R"(
<div style="min-height:180px; display:flex; align-items:center; justify-content:center; padding:12px 8px;">
  <div style="width:100%; border:1px dashed #d5d5d5; border-radius:12px; background:#fcfcfc; padding:18px 16px; text-align:center;">
    <div style="font-size:14px; font-weight:700; color:#3a3a3a; margin-bottom:6px;">引用片段会显示在这里</div>
    <div style="font-size:12px; color:#8a8a8a; line-height:1.6;">发起一次问答后，命中的文档片段、分数和 Markdown 内容会在右侧展开。</div>
  </div>
</div>
)");
        html += "</div>";
        referenceView_->setHtml(html);
        return;
    }
    for (const auto &ref: refs) {
        const QJsonObject obj = ref.toObject();
        const QString filename = obj.value("filename").toString();
        const double score = obj.value("score").toDouble();
        const QString text = obj.value("text").toString();
        const QString safeFilename = filename.isEmpty() ? "unknown" : filename.toHtmlEscaped();
        const QString contentHtml = markdownToHtmlFragment(text);

        html += QString::fromUtf8(R"(
<div style="margin-bottom:10px; border:1px solid #d8d8d8; border-radius:8px; background:#ffffff; padding:10px;">
  <div style="font-size:12px; color:#7b7b7b; margin-bottom:6px;">
    <b style="color:#3d3d3d;">%1</b>
    <span style="float:right;">score %2</span>
  </div>
  <div style="font-size:13px; line-height:1.45; color:#1f1f1f;">%3</div>
</div>
)")
                .arg(safeFilename)
                .arg(score, 0, 'f', 3)
                .arg(contentHtml);
    }
    html += "</div>";
    referenceView_->setHtml(html);
#endif
}

void ReferencePanel::clearReferences() {
    if (!referenceView_) {
        return;
    }
#ifdef QTRAG_CLIENT_HAS_WEBENGINE
    pendingRefsJson_ = "[]";
    renderReferencesJson(pendingRefsJson_);
#else
    referenceView_->clear();
#endif
}

#ifdef QTRAG_CLIENT_HAS_WEBENGINE
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
    }
    .ref-head {
      display: flex;
      justify-content: space-between;
      gap: 8px;
      font-size: 12px;
      color: var(--muted);
      margin-bottom: 6px;
    }
    .ref-file {
      color: #3d3d3d;
      font-weight: 700;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
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
#else
QString ReferencePanel::markdownToHtmlFragment(const QString &markdown) const {
    if (markdown.isEmpty()) {
        return QString("<p>&nbsp;</p>");
    }
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    // 回退模式：用 QTextDocument 转 Markdown，同时保留基础公式占位样式。
    struct PlaceholderPair {
        QString token;
        QString html;
    };
    QVector<PlaceholderPair> placeholders;
    QString markdownWithPlaceholders = markdown;
    int placeholderIndex = 0;

    auto replaceMathBlock = [&]() {
        QRegularExpression blockExpr(QStringLiteral("\\$\\$([\\s\\S]*?)\\$\\$"));
        blockExpr.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
        int searchPos = 0;
        while (true) {
            const QRegularExpressionMatch match = blockExpr.match(markdownWithPlaceholders, searchPos);
            if (!match.hasMatch()) {
                break;
            }
            const QString formulaRaw = match.captured(1).trimmed();
            const QString formulaEscaped = formulaRaw.toHtmlEscaped();
            const QString token = QString("QTRAG_REF_MATH_BLOCK_%1").arg(placeholderIndex++);
            placeholders.push_back({token, QString("<div class=\"math-block\">%1</div>").arg(formulaEscaped)});
            markdownWithPlaceholders.replace(match.capturedStart(0), match.capturedLength(0), token);
            searchPos = match.capturedStart(0) + token.size();
        }
    };

    auto replaceMathInline = [&]() {
        QRegularExpression inlineExpr(QStringLiteral("(?<!\\$)\\$([^\\n$]+?)\\$(?!\\$)"));
        int searchPos = 0;
        while (true) {
            const QRegularExpressionMatch match = inlineExpr.match(markdownWithPlaceholders, searchPos);
            if (!match.hasMatch()) {
                break;
            }
            const QString formulaRaw = match.captured(1).trimmed();
            const QString formulaEscaped = formulaRaw.toHtmlEscaped();
            const QString token = QString("QTRAG_REF_MATH_INLINE_%1").arg(placeholderIndex++);
            placeholders.push_back({token, QString("<span class=\"math-inline\">%1</span>").arg(formulaEscaped)});
            markdownWithPlaceholders.replace(match.capturedStart(0), match.capturedLength(0), token);
            searchPos = match.capturedStart(0) + token.size();
        }
    };

    replaceMathBlock();
    replaceMathInline();

    QTextDocument document;
    document.setMarkdown(markdownWithPlaceholders);
    QString html = document.toHtml();
    const int bodyStartTag = html.indexOf("<body");
    QString fragment = html;
    if (bodyStartTag >= 0) {
        const int bodyStart = html.indexOf('>', bodyStartTag);
        if (bodyStart >= 0) {
            const int bodyEnd = html.indexOf("</body>", bodyStart);
            fragment = (bodyEnd >= 0)
                           ? html.mid(bodyStart + 1, bodyEnd - bodyStart - 1)
                           : html.mid(bodyStart + 1);
        }
    }

    fragment.replace("<table>", "<table class=\"md-table\">");
    fragment.replace("<table ", "<table class=\"md-table\" ");
    for (const auto &placeholder : placeholders) {
        fragment.replace(placeholder.token, placeholder.html);
    }
    return fragment;
#else
    QString escaped = markdown.toHtmlEscaped();
    escaped.replace('\n', "<br/>");
    return QString("<p>%1</p>").arg(escaped);
#endif
}
#endif
