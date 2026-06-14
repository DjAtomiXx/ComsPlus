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
std::string encodePayload(ChatMessage const& message);
std::optional<ChatMessage> decodePayload(std::string const& payload);

} // namespace comsplus
