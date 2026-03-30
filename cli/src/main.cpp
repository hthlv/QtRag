#include <QCoreApplication>
#include <QCommandLineParser>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

namespace {

struct HttpResult {
    bool ok{false};
    int statusCode{0};
    QByteArray body;
    QString error;
};

QString trimTrailingSlash(QString url)
{
    while (url.endsWith('/')) {
        url.chop(1);
    }
    return url;
}

void printError(const QString &message)
{
    QTextStream(stderr) << "Error: " << message << Qt::endl;
}

void printLine(const QString &message = QString())
{
    QTextStream(stdout) << message << Qt::endl;
}

QByteArray readAllStdin()
{
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        return {};
    }
    return input.readAll();
}

QString extractJsonErrorMessage(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    const QJsonObject obj = doc.object();
    const QString message = obj.value("message").toString().trimmed();
    if (!message.isEmpty()) {
        return message;
    }
    return {};
}

HttpResult performRequest(QNetworkAccessManager &manager,
                          const QString &method,
                          const QUrl &url,
                          const QList<QPair<QByteArray, QByteArray>> &headers,
                          const QByteArray &body = {})
{
    // CLI 侧统一走这一层，把同步等待、状态码和错误体提取收敛到一个地方。
    QNetworkRequest request(url);
    for (const auto &header : headers) {
        request.setRawHeader(header.first, header.second);
    }

    QNetworkReply *reply = nullptr;
    const QString upperMethod = method.trimmed().toUpper();
    if (upperMethod == "GET") {
        reply = manager.get(request);
    } else if (upperMethod == "POST") {
        reply = manager.post(request, body);
    } else {
        return {false, 0, {}, QString("unsupported method: %1").arg(method)};
    }

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    HttpResult result;
    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    if (reply->error() == QNetworkReply::NoError) {
        result.ok = true;
    } else {
        result.error = reply->errorString();
        const QString serverMessage = extractJsonErrorMessage(result.body);
        if (!serverMessage.isEmpty()) {
            result.error += " | " + serverMessage;
        }
    }
    reply->deleteLater();
    return result;
}

void printJson(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error == QJsonParseError::NoError) {
        printLine(QString::fromUtf8(doc.toJson(QJsonDocument::Indented)));
        return;
    }
    printLine(QString::fromUtf8(body));
}

bool ensureSuccess(const HttpResult &result)
{
    if (result.ok) {
        return true;
    }
    QString message = result.error;
    if (message.isEmpty()) {
        message = QString("request failed with status %1").arg(result.statusCode);
    }
    printError(message);
    if (!result.body.isEmpty()) {
        printJson(result.body);
    }
    return false;
}

QString requireValue(const QStringList &args, int index, const QString &name)
{
    if (index >= 0 && index < args.size()) {
        return args.at(index);
    }
    printError(QString("missing argument: %1").arg(name));
    return {};
}

QByteArray readUtf8File(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QString("cannot open file: %1").arg(path);
        }
        return {};
    }
    const QByteArray data = file.readAll();
    const QString decoded = QString::fromUtf8(data.constData(), data.size());
    if (decoded.toUtf8() != data) {
        if (error) {
            *error = QString("file is not valid UTF-8 text: %1").arg(path);
        }
        return {};
    }
    return data;
}

class StreamRunner : public QObject {
public:
    StreamRunner(QNetworkAccessManager &manager,
                 QString serverBaseUrl,
                 QString query,
                 int topK,
                 QString llmId)
        : manager_(manager),
          serverBaseUrl_(std::move(serverBaseUrl)),
          query_(std::move(query)),
          topK_(topK),
          llmId_(std::move(llmId))
    {
    }

    int run()
    {
        // SSE 聊天需要持续消费服务端事件流，因此单独封装成一个运行器。
        QNetworkRequest request(QUrl(serverBaseUrl_ + "/api/v1/chat/stream"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
        request.setRawHeader("X-Top-K", QByteArray::number(topK_));
        if (!llmId_.trimmed().isEmpty()) {
            request.setRawHeader("X-LLM-Id", llmId_.trimmed().toUtf8());
        }

        reply_ = manager_.post(request, query_.toUtf8());
        QObject::connect(reply_, &QNetworkReply::readyRead, [this]() {
            buffer_.append(reply_->readAll());
            buffer_.replace("\r\n", "\n");
            processBuffer();
        });
        QObject::connect(reply_, &QNetworkReply::finished, [this]() {
            buffer_.append(reply_->readAll());
            buffer_.replace("\r\n", "\n");
            processBuffer();

            if (reply_->error() != QNetworkReply::NoError) {
                printError(reply_->errorString());
                const QByteArray body = reply_->readAll();
                if (!body.isEmpty()) {
                    printJson(body);
                }
                exitCode_ = 1;
            }

            if (!references_.isEmpty()) {
                printLine();
                printLine("References:");
                printReferenceList(references_);
            }
            reply_->deleteLater();
            loop_.quit();
        });

        loop_.exec();
        return exitCode_;
    }

private:
    void processBuffer()
    {
        // SSE 事件之间以空行分隔；网络分片可能把一个事件拆开，所以先缓冲再切块。
        while (true) {
            const int pos = buffer_.indexOf("\n\n");
            if (pos < 0) {
                return;
            }
            const QByteArray block = buffer_.left(pos);
            buffer_.remove(0, pos + 2);
            handleBlock(QString::fromUtf8(block));
        }
    }

    void handleBlock(const QString &block)
    {
        QString eventName;
        QString dataLine;
        const QStringList lines = block.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.startsWith("event:")) {
                eventName = line.mid(6).trimmed();
            } else if (line.startsWith("data:")) {
                if (!dataLine.isEmpty()) {
                    dataLine += '\n';
                }
                dataLine += line.mid(5).trimmed();
            }
        }

        if (eventName == "token") {
            // token 事件只追加增量文本，不等整段回答完成。
            const QJsonDocument doc = QJsonDocument::fromJson(dataLine.toUtf8());
            if (doc.isObject()) {
                QTextStream(stdout) << doc.object().value("content").toString();
                QTextStream(stdout).flush();
            }
            return;
        }

        if (eventName == "refs") {
            // refs 在流末尾附近返回，收集后统一打印，避免和 token 输出交错。
            const QJsonDocument doc = QJsonDocument::fromJson(dataLine.toUtf8());
            if (doc.isObject()) {
                references_ = doc.object().value("references").toArray();
            }
            return;
        }

        if (eventName == "error") {
            printLine();
            printError(dataLine);
            exitCode_ = 1;
            return;
        }

        if (eventName == "done") {
            printLine();
        }
    }

    static void printReferenceList(const QJsonArray &refs)
    {
        for (const QJsonValue &value : refs) {
            const QJsonObject obj = value.toObject();
            const QString filename = obj.value("filename").toString();
            const QString docId = obj.value("doc_id").toString();
            const QString score = QString::number(obj.value("score").toDouble(), 'f', 6);
            const QString text = obj.value("text").toString();
            printLine(QString("- [%1] %2 score=%3").arg(docId, filename, score));
            printLine(QString("  %1").arg(text));
        }
    }

    QNetworkAccessManager &manager_;
    QString serverBaseUrl_;
    QString query_;
    int topK_{3};
    QString llmId_;
    QNetworkReply *reply_{nullptr};
    QEventLoop loop_;
    QByteArray buffer_;
    QJsonArray references_;
    int exitCode_{0};
};

void printUsageExamples()
{
    printLine("Examples:");
    printLine("  QtRagCli health");
    printLine("  QtRagCli models");
    printLine("  QtRagCli docs list");
    printLine("  QtRagCli docs upload ./demo.md");
    printLine("  QtRagCli docs remove doc_123");
    printLine("  QtRagCli retrieve \"这个项目做什么\" --top-k 5");
    printLine("  QtRagCli chat \"请结合知识库回答\" --llm-id default");
    printLine("  QtRagCli chat \"请流式回答\" --stream");
    printLine("  QtRagCli embeddings regenerate --doc-id doc_123");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("QtRagCli");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("CLI client for QtRag server");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption serverOption({"s", "server"}, "Server base url", "url", "http://127.0.0.1:8080");
    QCommandLineOption topKOption({"k", "top-k"}, "Top K for retrieve/chat", "number", "3");
    QCommandLineOption kbOption(QStringList{"kb"}, "Knowledge base id for document upload", "kb_id", "default");
    QCommandLineOption fileNameOption(QStringList{"filename"}, "Override upload filename", "name");
    QCommandLineOption llmIdOption(QStringList{"llm-id"}, "LLM id to use for chat", "id");
    QCommandLineOption streamOption(QStringList{"stream"}, "Use SSE chat stream");
    QCommandLineOption docIdOption(QStringList{"doc-id"}, "Document id", "doc_id");
    QCommandLineOption stdinOption(QStringList{"stdin"}, "Read request body or query from stdin");

    parser.addOption(serverOption);
    parser.addOption(topKOption);
    parser.addOption(kbOption);
    parser.addOption(fileNameOption);
    parser.addOption(llmIdOption);
    parser.addOption(streamOption);
    parser.addOption(docIdOption);
    parser.addOption(stdinOption);
    parser.addPositionalArgument("command", "Command to execute.");
    parser.addPositionalArgument("args", "Command arguments.");
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        printUsageExamples();
        return 1;
    }

    const QString serverBaseUrl = trimTrailingSlash(parser.value(serverOption).trimmed());
    if (serverBaseUrl.isEmpty()) {
        printError("server url cannot be empty");
        return 1;
    }

    bool topKOk = false;
    const int topK = parser.value(topKOption).toInt(&topKOk);
    if (!topKOk || topK <= 0) {
        printError("--top-k must be a positive integer");
        return 1;
    }

    QNetworkAccessManager manager;
    const QString command = args.at(0);

    // 命令分发保持扁平，便于后续继续补更多 server 子命令。
    if (command == "health") {
        const auto result = performRequest(manager, "GET", QUrl(serverBaseUrl + "/health"), {});
        if (!ensureSuccess(result)) {
            return 1;
        }
        printJson(result.body);
        return 0;
    }

    if (command == "models") {
        const auto result = performRequest(manager, "GET", QUrl(serverBaseUrl + "/api/v1/models"), {});
        if (!ensureSuccess(result)) {
            return 1;
        }
        printJson(result.body);
        return 0;
    }

    if (command == "docs") {
        const QString action = requireValue(args, 1, "docs action");
        if (action.isEmpty()) {
            return 1;
        }

        if (action == "list") {
            const auto result = performRequest(manager, "GET", QUrl(serverBaseUrl + "/api/v1/docs"), {});
            if (!ensureSuccess(result)) {
                return 1;
            }
            printJson(result.body);
            return 0;
        }

        if (action == "upload") {
            QByteArray body;
            QString filename = parser.value(fileNameOption).trimmed();
            if (parser.isSet(stdinOption)) {
                // 支持管道上传，方便把别的命令输出直接送进知识库。
                body = readAllStdin();
                if (filename.isEmpty()) {
                    filename = "stdin.txt";
                }
            } else {
                const QString path = requireValue(args, 2, "file path");
                if (path.isEmpty()) {
                    return 1;
                }
                QString error;
                body = readUtf8File(path, &error);
                if (!error.isEmpty()) {
                    printError(error);
                    return 1;
                }
                if (filename.isEmpty()) {
                    filename = QFileInfo(path).fileName();
                }
            }

            if (body.isEmpty()) {
                printError("upload body is empty");
                return 1;
            }

            const auto result = performRequest(
                manager,
                "POST",
                QUrl(serverBaseUrl + "/api/v1/docs/upload"),
                {
                    {"Content-Type", "text/plain; charset=utf-8"},
                    {"X-Filename", filename.toUtf8()},
                    {"X-Kb-Id", parser.value(kbOption).trimmed().toUtf8()}
                },
                body);
            if (!ensureSuccess(result)) {
                return 1;
            }
            printJson(result.body);
            return 0;
        }

        if (action == "remove") {
            QString docId = parser.value(docIdOption).trimmed();
            if (docId.isEmpty()) {
                docId = requireValue(args, 2, "doc_id");
            }
            if (docId.isEmpty()) {
                return 1;
            }
            const auto result = performRequest(
                manager,
                "POST",
                QUrl(serverBaseUrl + "/api/v1/docs/remove"),
                {{"X-Doc-Id", docId.toUtf8()}});
            if (!ensureSuccess(result)) {
                return 1;
            }
            printJson(result.body);
            return 0;
        }

        printError(QString("unknown docs action: %1").arg(action));
        return 1;
    }

    if (command == "retrieve") {
        QString query;
        if (parser.isSet(stdinOption)) {
            query = QString::fromUtf8(readAllStdin()).trimmed();
        } else {
            query = requireValue(args, 1, "query");
        }
        if (query.trimmed().isEmpty()) {
            printError("query cannot be empty");
            return 1;
        }
        const auto result = performRequest(
            manager,
            "POST",
            QUrl(serverBaseUrl + "/api/v1/retrieve"),
            {{"X-Top-K", QByteArray::number(topK)}},
            query.toUtf8());
        if (!ensureSuccess(result)) {
            return 1;
        }
        printJson(result.body);
        return 0;
    }

    if (command == "chat") {
        QString query;
        if (parser.isSet(stdinOption)) {
            query = QString::fromUtf8(readAllStdin()).trimmed();
        } else {
            query = requireValue(args, 1, "query");
        }
        if (query.trimmed().isEmpty()) {
            printError("query cannot be empty");
            return 1;
        }

        if (parser.isSet(streamOption)) {
            // 流式聊天和普通 JSON 聊天的输出模型不同，直接走独立执行路径。
            StreamRunner runner(manager, serverBaseUrl, query, topK, parser.value(llmIdOption).trimmed());
            return runner.run();
        }

        QList<QPair<QByteArray, QByteArray>> headers{
            {"Content-Type", "text/plain; charset=utf-8"},
            {"X-Top-K", QByteArray::number(topK)}
        };
        if (!parser.value(llmIdOption).trimmed().isEmpty()) {
            headers.push_back({"X-LLM-Id", parser.value(llmIdOption).trimmed().toUtf8()});
        }
        const auto result = performRequest(
            manager,
            "POST",
            QUrl(serverBaseUrl + "/api/v1/chat"),
            headers,
            query.toUtf8());
        if (!ensureSuccess(result)) {
            return 1;
        }
        printJson(result.body);
        return 0;
    }

    if (command == "embeddings") {
        const QString action = requireValue(args, 1, "embeddings action");
        if (action.isEmpty()) {
            return 1;
        }
        if (action != "regenerate") {
            printError(QString("unknown embeddings action: %1").arg(action));
            return 1;
        }

        QList<QPair<QByteArray, QByteArray>> headers;
        const QString docId = parser.value(docIdOption).trimmed();
        if (!docId.isEmpty()) {
            headers.push_back({"X-Doc-Id", docId.toUtf8()});
        }
        const auto result = performRequest(
            manager,
            "POST",
            QUrl(serverBaseUrl + "/api/v1/embeddings/regenerate"),
            headers);
        if (!ensureSuccess(result)) {
            return 1;
        }
        printJson(result.body);
        return 0;
    }

    printError(QString("unknown command: %1").arg(command));
    printUsageExamples();
    return 1;
}
