//
// Created by hyp on 2026/3/28.
//

#pragma once
#include <QString>
#include <QVector>
class SessionRepository;
class MessageRepository;
struct SessionRecord;
struct MessageRecord;
class SessionController {
public:
    SessionController(SessionRepository *sessionRepo, MessageRepository *messageRepo);
    // 创建会话
    QString createSession(const QString &title = QString());
    // 加载会话
    QVector<SessionRecord> loadSessions() const;
    // 加载会话的消息
    QVector<MessageRecord> loadMessages(const QString & sessionId) const;
    // 保存用户消息
    bool saveUserMessage(const QString &sessionId, const QString& content);
    // 保存AI的消息
    bool saveAssistantMessage(const QString &sessionId, const QString &content);
    bool touchSession(const QString &sessionId);
    // 生成会话标题
    QString generateSessionTitle(const QString &firstUserMessage) const;
private:
    bool saveMessage(const QString &sessionId,
                     const QString &role,
                     const QString &content,
                     const QString &status);
    bool renameSessionFromFirstExchangeIfNeeded(const QString &sessionId);
    bool isDefaultSessionTitle(const QString &title) const;

    SessionRepository *sessionRepo_;
    MessageRepository *messageRepo_;
};
