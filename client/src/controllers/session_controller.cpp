//
// Created by hyp on 2026/3/28.
//

#include "session_controller.h"
#include "storage/repositories/session_repository.h"
#include "storage/repositories/message_repository.h"
#include "models/session_record.h"
#include "models/message_record.h"

#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>

SessionController::SessionController(SessionRepository *sessionRepo,
                                     MessageRepository *messageRepo)
    : sessionRepo_(sessionRepo), messageRepo_(messageRepo) {
}

QString SessionController::createSession(const QString &title) {
    if (!sessionRepo_) {
        return {};
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    SessionRecord session;
    session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    session.title = title.trimmed().isEmpty() ? generateSessionTitle(QString()) : title.trimmed();
    session.kb_id = "default";
    session.created_at = now;
    session.updated_at = now;
    if (!sessionRepo_->insert(session)) {
        return {};
    }
    return session.id;
}

QVector<SessionRecord> SessionController::loadSessions() const {
    if (!sessionRepo_) {
        return {};
    }
    return sessionRepo_->listAll();
}

QVector<MessageRecord> SessionController::loadMessages(const QString &sessionId) const {
    if (!messageRepo_ || sessionId.trimmed().isEmpty()) {
        return {};
    }
    return messageRepo_->listBySessionId(sessionId);
}

bool SessionController::saveUserMessage(const QString &sessionId, const QString &content) {
    return saveMessage(sessionId, "user", content, "done");
}

bool SessionController::saveAssistantMessage(const QString &sessionId, const QString &content) {
    if (!saveMessage(sessionId, "assistant", content, "done")) {
        return false;
    }
    return renameSessionFromFirstExchangeIfNeeded(sessionId);
}

bool SessionController::touchSession(const QString &sessionId) {
    if (!sessionRepo_ || sessionId.trimmed().isEmpty()) {
        return false;
    }
    return sessionRepo_->touch(sessionId, QDateTime::currentSecsSinceEpoch());
}

QString SessionController::generateSessionTitle(const QString &firstUserMessage) const {
    QString normalized = firstUserMessage;
    normalized.replace(QRegularExpression("\\s+"), " ");
    normalized = normalized.trimmed();
    if (normalized.isEmpty()) {
        return "新会话 " + QDateTime::currentDateTime().toString("MM-dd HH:mm:ss");
    }
    constexpr int maxTitleLength = 24;
    if (normalized.size() > maxTitleLength) {
        normalized = normalized.left(maxTitleLength).trimmed() + "...";
    }
    return normalized;
}

bool SessionController::saveMessage(const QString &sessionId,
                                    const QString &role,
                                    const QString &content,
                                    const QString &status) {
    if (!messageRepo_ || sessionId.trimmed().isEmpty() || content.trimmed().isEmpty()) {
        return false;
    }
    MessageRecord message;
    message.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    message.session_id = sessionId;
    message.role = role;
    message.content = content;
    message.status = status;
    message.created_at = QDateTime::currentSecsSinceEpoch();
    if (!messageRepo_->insert(message)) {
        return false;
    }
    return touchSession(sessionId);
}

bool SessionController::renameSessionFromFirstExchangeIfNeeded(const QString &sessionId) {
    if (!sessionRepo_ || !messageRepo_ || sessionId.trimmed().isEmpty()) {
        return false;
    }
    const auto session = sessionRepo_->findById(sessionId);
    if (!session.has_value()) {
        return false;
    }
    if (!isDefaultSessionTitle(session->title)) {
        return true;
    }

    const auto messages = messageRepo_->listBySessionId(sessionId);
    QString firstUserMessage;
    bool hasAssistantMessage = false;
    for (const auto &message: messages) {
        if (firstUserMessage.isEmpty() && message.role == "user" && !message.content.trimmed().isEmpty()) {
            firstUserMessage = message.content;
        }
        if (message.role == "assistant" && !message.content.trimmed().isEmpty()) {
            hasAssistantMessage = true;
        }
        if (!firstUserMessage.isEmpty() && hasAssistantMessage) {
            break;
        }
    }
    if (firstUserMessage.isEmpty() || !hasAssistantMessage) {
        return true;
    }

    const QString newTitle = generateSessionTitle(firstUserMessage);
    if (newTitle.isEmpty() || newTitle == session->title) {
        return true;
    }
    return sessionRepo_->updateTitle(sessionId, newTitle, QDateTime::currentSecsSinceEpoch());
}

bool SessionController::isDefaultSessionTitle(const QString &title) const {
    static const QRegularExpression defaultTitlePattern(
        "^新会话 \\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$"
    );
    return defaultTitlePattern.match(title.trimmed()).hasMatch();
}
