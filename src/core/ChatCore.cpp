#include "ChatCore.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <string_view>

namespace comsplus {
namespace {

constexpr int kProtocolVersion = 1;
constexpr std::size_t kMaxNameLength = 24;
constexpr std::size_t kMaxMessageLength = 120;

std::string sanitizeText(std::string const& input, std::size_t maxLength) {
    std::string out;
    out.reserve(std::min(input.size(), maxLength));
    bool pendingSpace = false;

    for (unsigned char ch : input) {
        if (std::isspace(ch) != 0) {
            pendingSpace = !out.empty();
            continue;
        }

        if (ch < 0x20 || ch == 0x7f) {
            continue;
        }

        if (pendingSpace && out.size() < maxLength) {
            out.push_back(' ');
        }
        pendingSpace = false;

        if (out.size() >= maxLength) {
            break;
        }
        out.push_back(static_cast<char>(ch));
    }

    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string lowercaseAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string escapeJson(std::string const& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char ch : input) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    return out;
}

std::optional<std::string> extractString(std::string_view payload, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\":\"";
    auto start = payload.find(needle);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    start += needle.size();

    std::string out;
    bool escaped = false;
    for (std::size_t i = start; i < payload.size(); ++i) {
        char ch = payload[i];
        if (escaped) {
            switch (ch) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    out.push_back(ch);
                    break;
            }
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return out;
        }
        out.push_back(ch);
    }

    return std::nullopt;
}

std::optional<std::int64_t> extractInt(std::string_view payload, std::string_view key) {
    std::string needle = "\"" + std::string(key) + "\":";
    auto start = payload.find(needle);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    start += needle.size();

    auto end = start;
    if (end < payload.size() && payload[end] == '-') {
        ++end;
    }
    while (end < payload.size() && std::isdigit(static_cast<unsigned char>(payload[end])) != 0) {
        ++end;
    }
    if (end == start) {
        return std::nullopt;
    }

    std::int64_t value = 0;
    auto result = std::from_chars(payload.data() + start, payload.data() + end, value);
    if (result.ec != std::errc()) {
        return std::nullopt;
    }
    return value;
}

std::string_view kindToWire(ChatMessageKind kind) {
    return kind == ChatMessageKind::System ? "system" : "user";
}

ChatMessageKind parseKind(std::optional<std::string> const& kind) {
    if (kind && *kind == "system") {
        return ChatMessageKind::System;
    }
    return ChatMessageKind::User;
}

} // namespace

RateLimiter::RateLimiter(std::int64_t cooldownMs)
    : m_cooldownMs(std::max<std::int64_t>(0, cooldownMs)) {}

bool RateLimiter::canSend(std::int64_t nowMs) const {
    return !m_lastSentMs.has_value() || nowMs - *m_lastSentMs >= m_cooldownMs;
}

void RateLimiter::markSent(std::int64_t nowMs) {
    m_lastSentMs = nowMs;
}

std::string sanitizeName(std::string const& input) {
    return sanitizeText(input, kMaxNameLength);
}

std::string sanitizeMessage(std::string const& input) {
    return sanitizeText(input, kMaxMessageLength);
}

std::string selectDisplayName(std::string const& realName, DisplayNameSettings const& settings) {
    auto real = sanitizeName(realName);
    auto fake = sanitizeName(settings.fakeName);

    if (settings.mode == ChatNameMode::Real) {
        return real.empty() ? "Player" : real;
    }

    if (settings.mode == ChatNameMode::Fake) {
        return fake.empty() ? (real.empty() ? "Player" : real) : fake;
    }

    if (settings.privacyEnabled && !fake.empty()) {
        return fake;
    }

    return real.empty() ? "Player" : real;
}

std::string replaceOwnNameText(std::string const& input, std::string const& realName, std::string const& fakeName) {
    auto real = sanitizeName(realName);
    auto fake = sanitizeName(fakeName);
    if (real.size() < 3 || fake.empty() || lowercaseAscii(real) == lowercaseAscii(fake)) {
        return input;
    }

    auto text = input;
    auto lowerText = lowercaseAscii(text);
    auto lowerReal = lowercaseAscii(real);
    auto lowerFake = lowercaseAscii(fake);

    std::size_t pos = 0;
    while ((pos = lowerText.find(lowerReal, pos)) != std::string::npos) {
        text.replace(pos, real.size(), fake);
        lowerText.replace(pos, lowerReal.size(), lowerFake);
        pos += fake.size();
    }
    return text;
}

std::string encodePayload(ChatMessage const& message) {
    ChatMessage sanitized = message;
    sanitized.protocolVersion = kProtocolVersion;
    sanitized.messageId = sanitizeText(sanitized.messageId, 48);
    sanitized.displayName = sanitizeName(sanitized.displayName);
    sanitized.iconData = sanitizeText(sanitized.iconData, 64);
    sanitized.text = sanitizeMessage(sanitized.text);

    std::ostringstream out;
    out << "{"
        << "\"v\":" << sanitized.protocolVersion << ","
        << "\"kind\":\"" << kindToWire(sanitized.kind) << "\","
        << "\"id\":\"" << escapeJson(sanitized.messageId) << "\","
        << "\"aid\":" << sanitized.accountId << ","
        << "\"name\":\"" << escapeJson(sanitized.displayName) << "\","
        << "\"icon\":\"" << escapeJson(sanitized.iconData) << "\","
        << "\"text\":\"" << escapeJson(sanitized.text) << "\","
        << "\"ts\":" << sanitized.timestamp
        << "}";
    return out.str();
}

std::optional<ChatMessage> decodePayload(std::string const& payload) {
    if (payload.size() > 512 || payload.empty() || payload.front() != '{') {
        return std::nullopt;
    }

    auto version = extractInt(payload, "v");
    if (!version.has_value() || *version != kProtocolVersion) {
        return std::nullopt;
    }

    auto messageId = extractString(payload, "id");
    auto accountId = extractInt(payload, "aid");
    auto name = extractString(payload, "name");
    auto icon = extractString(payload, "icon");
    auto text = extractString(payload, "text");
    auto timestamp = extractInt(payload, "ts");
    auto kind = extractString(payload, "kind");

    if (!messageId || !accountId || !name || !icon || !text || !timestamp) {
        return std::nullopt;
    }

    ChatMessage message;
    message.protocolVersion = static_cast<int>(*version);
    message.kind = parseKind(kind);
    message.messageId = sanitizeText(*messageId, 48);
    message.accountId = *accountId;
    message.displayName = sanitizeName(*name);
    message.iconData = sanitizeText(*icon, 64);
    message.text = sanitizeMessage(*text);
    message.timestamp = *timestamp;

    if (message.messageId.empty() || message.displayName.empty() || message.text.empty()) {
        return std::nullopt;
    }

    return message;
}

} // namespace comsplus
