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
constexpr auto kDefaultServerUrl = "https://hexasystems.xyz/comsplus";

std::int64_t nowMs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string normalizedServerUrl(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url.empty() ? kDefaultServerUrl : url;
}

std::string endpoint(std::string const& serverUrl, std::string const& pathAndQuery) {
    return serverUrl + pathAndQuery;
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

    auto serverUrl = normalizedServerUrl(settings.mainChatServerUrl);
    if (serverUrl.empty()) {
        std::lock_guard lock(m_mutex);
        m_status = "Main chat connecting";
        return;
    }

    auto current = nowMs();
    auto since = std::int64_t{0};
    {
        std::lock_guard lock(m_mutex);
        if (m_polling || current - m_lastPollMs < 1250) return;
        m_polling = true;
        m_lastPollMs = current;
        since = m_lastSeen;
    }

    startPoll(std::move(serverUrl), since);
}

std::string GlobalChatBridge::statusText() const {
    std::lock_guard lock(m_mutex);
    return m_status;
}

ChatSendResult GlobalChatBridge::sendChat(ChatMessage const& message) {
    auto settings = readSettings();
    auto serverUrl = normalizedServerUrl(settings.mainChatServerUrl);
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

void GlobalChatBridge::startPoll(std::string serverUrl, std::int64_t since) {
    std::thread([this, serverUrl = std::move(serverUrl), since] {
        auto status = isLocalServerUrl(serverUrl) ? std::string("Set public server") : std::string("Main chat offline");
        auto received = std::vector<ChatMessage>{};
        auto lastSeen = since;

        auto response = web::WebRequest()
            .timeout(std::chrono::seconds(4))
            .userAgent("ComsPlus/1.0")
            .getSync(endpoint(serverUrl, "/poll?room=main&since=" + std::to_string(since)));

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
