#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace comsplus {

enum class ChatNameMode {
    Auto,
    Real,
    Fake
};

enum class ChatMessageKind {
    User,
    System,
    Moderation
};

enum class ChatColorMode {
    Default,
    Custom,
    Rainbow
};

enum class ChatAuthorRole {
    None,
    Dev
};

enum class ChatModerationAction {
    None,
    Ban,
    TempBan
};

enum class ChatSendResult {
    Sent,
    Queued,
    Failed
};

struct DisplayNameSettings {
    bool privacyEnabled = false;
    std::string fakeName;
    ChatNameMode mode = ChatNameMode::Auto;
};

struct ChatMessage {
    int protocolVersion = 1;
    std::string messageId;
    std::int64_t accountId = 0;
    std::string displayName;
    std::string iconData;
    std::string text;
    std::int64_t timestamp = 0;
    ChatMessageKind kind = ChatMessageKind::User;
    std::int64_t levelId = 0;
    std::string levelName;
    ChatColorMode colorMode = ChatColorMode::Default;
    std::string primaryColor;
    std::string secondaryColor;
    ChatAuthorRole authorRole = ChatAuthorRole::None;
    ChatModerationAction moderationAction = ChatModerationAction::None;
    std::string targetName;
    std::int64_t targetAccountId = 0;
    std::int64_t expiresAt = 0;
};

class RateLimiter {
public:
    explicit RateLimiter(std::int64_t cooldownMs);

    bool canSend(std::int64_t nowMs) const;
    void markSent(std::int64_t nowMs);

private:
    std::int64_t m_cooldownMs;
    std::optional<std::int64_t> m_lastSentMs;
};

std::string sanitizeName(std::string const& input);
std::string sanitizeMessage(std::string const& input);
std::string selectDisplayName(std::string const& realName, DisplayNameSettings const& settings);
std::string replaceOwnNameText(std::string const& input, std::string const& realName, std::string const& fakeName);
std::string encodePayload(ChatMessage const& message);
std::optional<ChatMessage> decodePayload(std::string const& payload);

} // namespace comsplus
