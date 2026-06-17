#include "GlobalChatBridge.hpp"

#include "ComsPlusSettings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <sstream>
#include <thread>

using namespace geode::prelude;

namespace comsplus {
namespace {

constexpr std::size_t kMaxGlobalMessages = 60;
constexpr std::size_t kMaxPresenceRows = 80;
constexpr std::size_t kMaxActivityRows = 60;
constexpr auto kDefaultServerUrl = "https://hexasystems.xyz/comsplus";

std::int64_t nowMs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string relayServerUrl() {
    return kDefaultServerUrl;
}

std::string endpoint(std::string const& serverUrl, std::string const& pathAndQuery) {
    return serverUrl + pathAndQuery;
}

std::string percentEncode(std::string const& value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~'
        ) {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[ch >> 4]);
            out.push_back(hex[ch & 0x0f]);
        }
    }
    return out;
}

std::string localDisplayName() {
    auto settings = readSettings();
    DisplayNameSettings displaySettings{
        settings.privacyEnabled,
        settings.fakeName,
        settings.chatNameMode
    };
    return selectDisplayName(localRealName(), displaySettings);
}

std::string heartbeatQuery(std::int64_t since) {
    return "/poll?room=main&since=" + std::to_string(since) +
        "&aid=" + std::to_string(localAccountId()) +
        "&name=" + percentEncode(localDisplayName()) +
        "&icon=" + percentEncode(localIconData());
}

bool isLocalServerUrl(std::string const& url) {
    auto lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lower.starts_with("http://127.") ||
        lower.starts_with("https://127.") ||
        lower.starts_with("http://localhost") ||
        lower.starts_with("https://localhost") ||
        lower.starts_with("http://0.0.0.0") ||
        lower.starts_with("https://0.0.0.0");
}

std::vector<std::string> linesOf(std::string const& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

std::vector<std::string> splitTabs(std::string const& text) {
    std::vector<std::string> parts;
    std::string current;
    std::istringstream stream(text);
    while (std::getline(stream, current, '\t')) {
        parts.push_back(current);
    }
    return parts;
}

std::vector<ChatPresence> parsePresence(std::string const& body) {
    std::vector<ChatPresence> out;
    auto lines = linesOf(body);
    for (std::size_t i = 1; i < lines.size() && out.size() < kMaxPresenceRows; ++i) {
        auto parts = splitTabs(lines[i]);
        if (parts.size() < 6) continue;
        ChatPresence row;
        row.accountId = numFromString<std::int64_t>(parts[0]).unwrapOr(0);
        row.displayName = sanitizeName(parts[1]);
        row.iconData = parts[2].substr(0, 64);
        row.joinedAt = numFromString<std::int64_t>(parts[3]).unwrapOr(0);
        row.lastSeen = numFromString<std::int64_t>(parts[4]).unwrapOr(0);
        row.messageCount = numFromString<int>(parts[5]).unwrapOr(0);
        row.usesComsPlus = true;
        if (!row.displayName.empty()) {
            out.push_back(std::move(row));
        }
    }
    return out;
}

std::vector<ChatActivity> parseActivity(std::string const& body) {
    std::vector<ChatActivity> out;
    auto lines = linesOf(body);
    for (std::size_t i = 1; i < lines.size() && out.size() < kMaxActivityRows; ++i) {
        auto parts = splitTabs(lines[i]);
        if (parts.size() < 2) continue;
        ChatActivity row;
        row.timestamp = numFromString<std::int64_t>(parts[0]).unwrapOr(0);
        row.text = sanitizeMessage(parts[1]);
        if (!row.text.empty()) {
            out.push_back(std::move(row));
        }
    }
    return out;
}

} // namespace

GlobalChatBridge& GlobalChatBridge::get() {
    static GlobalChatBridge bridge;
    return bridge;
}

void GlobalChatBridge::maintain() {
    auto settings = readSettings();
    if (!settings.mainMenuChatEnabled) {
        std::lock_guard lock(m_mutex);
        m_status = "Main chat disabled";
        return;
    }

    auto serverUrl = relayServerUrl();
    if (serverUrl.empty()) {
        std::lock_guard lock(m_mutex);
        m_status = "Main chat connecting";
        return;
    }

    auto current = nowMs();
    auto since = std::int64_t{0};
    auto loadMeta = false;
    {
        std::lock_guard lock(m_mutex);
        if (!m_polling && current - m_lastPollMs >= 1250) {
            m_polling = true;
            m_lastPollMs = current;
            since = m_lastSeen;
        }
        if (!m_metaLoading && current - m_lastMetaMs >= 5000) {
            m_metaLoading = true;
            m_lastMetaMs = current;
            loadMeta = true;
        }
    }

    if (since != 0 || current - m_lastPollMs < 20) {
        startPoll(serverUrl, since);
    }
    if (loadMeta) {
        startMeta(std::move(serverUrl));
    }
}

std::string GlobalChatBridge::statusText() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

ChatSendResult GlobalChatBridge::sendChat(ChatMessage const& message) {
    auto settings = readSettings();
    auto serverUrl = relayServerUrl();
    if (!settings.mainMenuChatEnabled || serverUrl.empty()) {
        std::lock_guard lock(m_mutex);
        m_status = "Main chat connecting";
        return ChatSendResult::Failed;
    }

    {
        std::lock_guard lock(m_mutex);
        m_status = "Main chat sending";
        m_sending = true;
    }
    startSend(std::move(serverUrl), message);
    return ChatSendResult::Queued;
}

std::vector<ChatMessage> GlobalChatBridge::pollReceived() {
    std::lock_guard lock(m_mutex);
    auto out = std::move(m_received);
    m_received.clear();
    return out;
}

std::vector<ChatPresence> GlobalChatBridge::presenceSnapshot() const {
    std::lock_guard lock(m_mutex);
    return m_presence;
}

std::vector<ChatActivity> GlobalChatBridge::activitySnapshot() const {
    std::lock_guard lock(m_mutex);
    return m_activity;
}

void GlobalChatBridge::startPoll(std::string serverUrl, std::int64_t since) {
    std::thread([this, serverUrl = std::move(serverUrl), since] {
        auto status = isLocalServerUrl(serverUrl) ? std::string("Set public server") : std::string("Main chat offline");
        auto received = std::vector<ChatMessage>{};
        auto lastSeen = since;

        auto response = web::WebRequest()
            .timeout(std::chrono::seconds(4))
            .userAgent("ComsPlus/1.0")
            .getSync(endpoint(serverUrl, heartbeatQuery(since)));

        if (response.ok()) {
            auto body = response.string();
            if (body) {
                auto lines = linesOf(body.unwrap());
                if (!lines.empty()) {
                    lastSeen = numFromString<std::int64_t>(lines.front()).unwrapOr(lastSeen);
                    for (std::size_t i = 1; i < lines.size(); ++i) {
                        auto decoded = decodePayload(lines[i]);
                        if (decoded.has_value()) {
                            received.push_back(std::move(*decoded));
                        }
                    }
                }
                status = "Main chat online";
            }
        }

        std::lock_guard lock(m_mutex);
        m_lastSeen = std::max(m_lastSeen, lastSeen);
        for (auto& message : received) {
            m_received.push_back(std::move(message));
        }
        if (m_received.size() > kMaxGlobalMessages) {
            m_received.erase(
                m_received.begin(),
                m_received.begin() + static_cast<long>(m_received.size() - kMaxGlobalMessages)
            );
        }
        m_status = std::move(status);
        m_polling = false;
    }).detach();
}

void GlobalChatBridge::startMeta(std::string serverUrl) {
    std::thread([this, serverUrl = std::move(serverUrl)] {
        auto presence = std::vector<ChatPresence>{};
        auto activity = std::vector<ChatActivity>{};

        auto presenceResponse = web::WebRequest()
            .timeout(std::chrono::seconds(4))
            .userAgent("ComsPlus/1.0")
            .getSync(endpoint(serverUrl, "/presence?room=main"));
        if (presenceResponse.ok()) {
            if (auto body = presenceResponse.string()) {
                presence = parsePresence(body.unwrap());
            }
        }

        auto activityResponse = web::WebRequest()
            .timeout(std::chrono::seconds(4))
            .userAgent("ComsPlus/1.0")
            .getSync(endpoint(serverUrl, "/activity?room=main"));
        if (activityResponse.ok()) {
            if (auto body = activityResponse.string()) {
                activity = parseActivity(body.unwrap());
            }
        }

        std::lock_guard lock(m_mutex);
        if (!presence.empty()) {
            m_presence = std::move(presence);
        }
        if (!activity.empty()) {
            m_activity = std::move(activity);
        }
        m_metaLoading = false;
    }).detach();
}

void GlobalChatBridge::startSend(std::string serverUrl, ChatMessage message) {
    std::thread([this, serverUrl = std::move(serverUrl), message = std::move(message)] {
        auto status = isLocalServerUrl(serverUrl) ? std::string("Set public server") : std::string("Main chat offline");
        auto payload = encodePayload(message);

        auto response = web::WebRequest()
            .timeout(std::chrono::seconds(4))
            .userAgent("ComsPlus/1.0")
            .header("Content-Type", "application/json")
            .bodyString(payload)
            .postSync(endpoint(serverUrl, "/send?room=main"));

        if (response.ok()) {
            status = "Main chat online";
        }

        std::lock_guard lock(m_mutex);
        m_status = std::move(status);
        m_sending = false;
    }).detach();
}

} // namespace comsplus
