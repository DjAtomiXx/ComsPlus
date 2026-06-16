#include "GlobedBridge.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <set>
#include <variant>

#if __has_include(<globed/soft-link/API.hpp>) && __has_include(<globed/core/data/Messages.hpp>)
    #include <globed/soft-link/API.hpp>
    #include <globed/core/data/Messages.hpp>
    #define COMSPLUS_WITH_GLOBED 1
#else
    #define COMSPLUS_WITH_GLOBED 0
#endif

using namespace geode::prelude;

namespace comsplus {
namespace {

constexpr uint16_t kEventType = 0xff42;
constexpr char const* kMinGlobedVersion = "2.1.4";
constexpr std::size_t kMaxPayloadLength = 768;
constexpr std::size_t kMaxReceivedMessages = 40;
constexpr std::size_t kMaxPendingMessages = 18;
constexpr std::size_t kMaxSeenMessages = 256;
std::set<std::string> g_seenMessages;

bool rememberMessage(std::string const& messageId) {
    if (messageId.empty()) return false;
    if (g_seenMessages.size() > kMaxSeenMessages) {
        g_seenMessages.clear();
    }
    return g_seenMessages.insert(messageId).second;
}

#if COMSPLUS_WITH_GLOBED
std::optional<globed::MessageListener<globed::msg::LevelDataMessage>> g_listener;
#endif

} // namespace

GlobedBridge& GlobedBridge::get() {
    static GlobedBridge bridge;
    return bridge;
}

void GlobedBridge::initialize() {
    if (m_initialized) return;
    m_initialized = true;

#if COMSPLUS_WITH_GLOBED
    maintain();
#else
    log::warn("ComsPlus built without Globed headers; network chat is disabled");
#endif
}

void GlobedBridge::installListener() {
#if COMSPLUS_WITH_GLOBED
    if (m_listenerInstalled) return;
    m_listenerInstalled = true;

    g_listener = globed::api::net::listen<globed::msg::LevelDataMessage>([this](globed::msg::LevelDataMessage& data) {
        for (auto const& event : data.events) {
            if (!event.is<globed::UnknownEvent>()) continue;
            auto const& unknown = event.as<globed::UnknownEvent>();
            if (unknown.type != kEventType) continue;
            if (unknown.rawData.size() > kMaxPayloadLength) continue;

            auto decoded = decodePayload(std::string(unknown.rawData.begin(), unknown.rawData.end()));
            if (!decoded.has_value()) continue;
            if (!rememberMessage(decoded->messageId)) continue;
            m_received.push_back(std::move(*decoded));
            if (m_received.size() > kMaxReceivedMessages) {
                m_received.erase(
                    m_received.begin(),
                    m_received.begin() + static_cast<long>(m_received.size() - kMaxReceivedMessages)
                );
            }
        }
        return true;
    });
#endif
}

void GlobedBridge::registerEvent() {
#if COMSPLUS_WITH_GLOBED
    if (!globed::api::available() || !globed::api::isAtLeast(kMinGlobedVersion)) return;
    m_eventRegistered = true;
#endif
}

void GlobedBridge::maintain() {
    initialize();

#if COMSPLUS_WITH_GLOBED
    if (!isAvailable()) return;

    if (!m_eventRegistered) {
        registerEvent();
    }
    installListener();

    if (!isConnected() || m_pending.empty()) return;

    auto pending = std::move(m_pending);
    m_pending.clear();
    for (auto const& message : pending) {
        if (!sendNow(message)) {
            queuePending(message);
        }
    }
#endif
}

bool GlobedBridge::isAvailable() const {
#if COMSPLUS_WITH_GLOBED
    return globed::api::available() && globed::api::isAtLeast(kMinGlobedVersion);
#else
    return false;
#endif
}

bool GlobedBridge::isConnected() const {
#if COMSPLUS_WITH_GLOBED
    return isAvailable() && globed::api::net::isConnected() && globed::api::game::isActive();
#else
    return false;
#endif
}

std::string GlobedBridge::statusText() const {
#if COMSPLUS_WITH_GLOBED
    auto queued = m_pending.empty() ? "" : " queued " + std::to_string(m_pending.size());
    if (!globed::api::available()) return "Globed not loaded";
    if (!globed::api::isAtLeast(kMinGlobedVersion)) return "Globed 2.1.4+ required";
    if (!globed::api::net::isConnected()) return "Globed offline" + queued;
    if (!globed::api::game::isActive()) return "Join a Globed level" + queued;
    return "Globed connected" + queued;
#else
    return "Globed bridge unavailable";
#endif
}

bool GlobedBridge::sendNow(ChatMessage const& message) {
#if COMSPLUS_WITH_GLOBED
    if (!isConnected()) return false;
    auto payload = encodePayload(message);
    if (!decodePayload(payload).has_value()) return false;

    rememberMessage(message.messageId);

    globed::UnknownEvent event;
    event.type = kEventType;
    event.rawData.assign(payload.begin(), payload.end());
    globed::api::net::queueGameEvent(globed::OutEvent(std::move(event)));
    return true;
#else
    (void)message;
    return false;
#endif
}

void GlobedBridge::queuePending(ChatMessage const& message) {
    if (message.messageId.empty()) return;
    auto exists = std::any_of(m_pending.begin(), m_pending.end(), [&](ChatMessage const& pending) {
        return pending.messageId == message.messageId;
    });
    if (exists) return;

    if (m_pending.size() >= kMaxPendingMessages) {
        m_pending.erase(m_pending.begin());
    }
    m_pending.push_back(message);
}

ChatSendResult GlobedBridge::sendChat(ChatMessage const& message) {
#if COMSPLUS_WITH_GLOBED
    maintain();

    auto payload = encodePayload(message);
    if (!decodePayload(payload).has_value()) return ChatSendResult::Failed;
    if (!isAvailable()) return ChatSendResult::Failed;

    if (!isConnected()) {
        queuePending(message);
        return ChatSendResult::Queued;
    }

    if (sendNow(message)) {
        return ChatSendResult::Sent;
    }
    queuePending(message);
    return ChatSendResult::Queued;
#else
    (void)message;
    return ChatSendResult::Failed;
#endif
}

std::vector<ChatMessage> GlobedBridge::pollReceived() {
    maintain();
    auto out = std::move(m_received);
    m_received.clear();
    return out;
}

} // namespace comsplus
