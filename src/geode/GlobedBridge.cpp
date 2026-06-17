#include "GlobedBridge.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <sstream>
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
constexpr std::int64_t kMinGlobedSendGapMs = 650;
std::set<std::string> g_seenMessages;
std::mutex g_seenMessagesMutex;
std::atomic<std::int64_t> g_lastCallbackLogMs{0};

std::int64_t steadyMs() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

bool rememberMessage(std::string const& messageId) {
    if (messageId.empty()) return false;
    std::lock_guard lock(g_seenMessagesMutex);
    if (g_seenMessages.size() > kMaxSeenMessages) {
        g_seenMessages.clear();
    }
    return g_seenMessages.insert(messageId).second;
}

#if COMSPLUS_WITH_GLOBED
std::optional<globed::MessageListener<globed::msg::LevelDataMessage>> g_listener;
#endif

#if COMSPLUS_WITH_GLOBED
std::string iconDataFor(globed::PlayerIconData const& icons) {
    std::ostringstream out;
    out << "cube:" << icons.cube
        << ":c1:" << icons.color1.asIdx()
        << ":c2:" << icons.color2.asIdx()
        << ":glow:" << icons.glowColor.asIdx();
    return out.str();
}

bool globedModLoaded() {
    auto loader = Loader::get();
    if (!loader) return false;
    auto mod = loader->getLoadedMod("dankmeme.globed2");
    return mod && mod->isOrWillBeEnabled();
}

bool globedApiReady() {
    if (!globedModLoaded()) return false;
    return globed::api::available() && globed::api::isAtLeast(kMinGlobedVersion);
}
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
    log::debug("ComsPlus Globed bridge initialized");
#else
    log::warn("ComsPlus built without Globed headers; network chat is disabled");
#endif
}

void GlobedBridge::installListener() {
#if COMSPLUS_WITH_GLOBED
    if (m_listenerInstalled && g_listener.has_value()) {
        log::debug("ComsPlus Globed listener already installed");
        return;
    }
    if (!globedApiReady()) {
        log::debug("ComsPlus skipped Globed listener install; API not ready");
        return;
    }

    g_listener = globed::api::net::listen<globed::msg::LevelDataMessage>([](globed::msg::LevelDataMessage& data) {
        return GlobedBridge::get().handleLevelDataMessage(&data);
    });
    m_listenerInstalled = true;
    log::info("ComsPlus Globed LevelData listener installed");
#endif
}

void GlobedBridge::uninstallListener() {
#if COMSPLUS_WITH_GLOBED
    if (g_listener.has_value()) {
        g_listener->reset();
        g_listener.reset();
        log::info("ComsPlus Globed listener cleaned up");
    }
    m_listenerInstalled = false;
    m_eventRegistered = false;
#endif
}

void GlobedBridge::shutdown() {
    uninstallListener();
    std::lock_guard lock(m_mutex);
    m_pending.clear();
    m_received.clear();
}

bool GlobedBridge::handleLevelDataMessage(void* message) {
#if COMSPLUS_WITH_GLOBED
    if (!message) {
        log::debug("ComsPlus Globed callback skipped null LevelDataMessage");
        return false;
    }
    if (!globedApiReady()) {
        log::debug("ComsPlus Globed callback skipped; API no longer ready");
        return false;
    }

    auto* data = static_cast<globed::msg::LevelDataMessage*>(message);
    auto current = steadyMs();
#ifdef GEODE_IS_ANDROID
    auto lastLog = g_lastCallbackLogMs.load();
    if (current - lastLog > 5000 && g_lastCallbackLogMs.compare_exchange_strong(lastLog, current)) {
        log::debug(
            "ComsPlus Globed LevelData callback: {} event(s), {} player state(s)",
            data->events.size(),
            data->players.size()
        );
    }
#endif

    std::vector<ChatMessage> decodedMessages;
    decodedMessages.reserve(std::min<std::size_t>(data->events.size(), 8));
    for (auto const& event : data->events) {
        if (!event.is<globed::UnknownEvent>()) continue;
        auto const& unknown = event.as<globed::UnknownEvent>();
        if (unknown.type != kEventType) continue;
        if (unknown.rawData.empty()) continue;
        if (unknown.rawData.size() > kMaxPayloadLength) {
            log::debug("ComsPlus skipped oversized Globed payload: {} bytes", unknown.rawData.size());
            continue;
        }

        auto payload = std::string(unknown.rawData.begin(), unknown.rawData.end());
        auto decoded = decodePayload(payload);
        if (!decoded.has_value()) {
            log::debug("ComsPlus skipped malformed Globed payload");
            continue;
        }
        if (!rememberMessage(decoded->messageId)) continue;
        decodedMessages.push_back(std::move(*decoded));
    }

    if (!decodedMessages.empty()) {
        std::lock_guard lock(m_mutex);
        for (auto& decoded : decodedMessages) {
            m_received.push_back(std::move(decoded));
        }
        if (m_received.size() > kMaxReceivedMessages) {
            m_received.erase(
                m_received.begin(),
                m_received.begin() + static_cast<long>(m_received.size() - kMaxReceivedMessages)
            );
        }
        log::debug("ComsPlus accepted {} Globed chat message(s)", decodedMessages.size());
    }
#endif
        // Propagate the LevelDataMessage. Returning true here stops later Globed listeners
        // and can make remote players disappear from levels.
        return false;
}

void GlobedBridge::registerEvent() {
#if COMSPLUS_WITH_GLOBED
    if (!globedApiReady()) {
        log::debug("ComsPlus skipped Globed event registration; API not ready");
        return;
    }
    m_eventRegistered = true;
    log::debug("ComsPlus Globed event channel ready");
#endif
}

void GlobedBridge::maintain() {
    initialize();

#if COMSPLUS_WITH_GLOBED
    if (!isAvailable()) {
        if (m_listenerInstalled) {
            uninstallListener();
        }
        return;
    }

    if (!m_eventRegistered) {
        registerEvent();
    }
    installListener();

    if (!isConnected()) return;
    {
        std::lock_guard lock(m_mutex);
        if (m_pending.empty()) return;
        if (steadyMs() - m_lastSendMs < kMinGlobedSendGapMs) return;
    }

    ChatMessage message;
    {
        std::lock_guard lock(m_mutex);
        if (m_pending.empty()) return;
        message = m_pending.front();
        m_pending.erase(m_pending.begin());
    }
    if (!sendNow(message)) {
        queuePending(message);
    }
#endif
}

bool GlobedBridge::isAvailable() const {
#if COMSPLUS_WITH_GLOBED
    return globedApiReady();
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
    auto queuedSize = std::size_t{0};
    {
        std::lock_guard lock(m_mutex);
        queuedSize = m_pending.size();
    }
    auto queued = queuedSize == 0 ? "" : " queued " + std::to_string(queuedSize);
    if (!globedModLoaded()) return "Globed not loaded";
    if (!globed::api::available()) return "Globed starting" + queued;
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
    {
        std::lock_guard lock(m_mutex);
        m_lastSendMs = steadyMs();
    }
    return true;
#else
    (void)message;
    return false;
#endif
}

void GlobedBridge::queuePending(ChatMessage const& message) {
    if (message.messageId.empty()) return;
    std::lock_guard lock(m_mutex);
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

    auto shouldQueueForRateLimit = false;
    {
        std::lock_guard lock(m_mutex);
        if (steadyMs() - m_lastSendMs < kMinGlobedSendGapMs) {
            shouldQueueForRateLimit = true;
        }
    }
    if (shouldQueueForRateLimit) {
        queuePending(message);
        return ChatSendResult::Queued;
    }

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
    std::lock_guard lock(m_mutex);
    auto out = std::move(m_received);
    m_received.clear();
    return out;
}

std::vector<ChatPresence> GlobedBridge::presenceSnapshot() const {
    std::vector<ChatPresence> out;
#if COMSPLUS_WITH_GLOBED
    if (!isConnected()) return out;
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    for (auto const& player : globed::api::game::getPlayers()) {
        auto* remote = player.get();
        if (!remote) {
            log::debug("ComsPlus skipped null Globed remote player");
            continue;
        }
        ChatPresence row;
        row.accountId = globed::api::player::getAccountId(remote);
        row.displayName = sanitizeName(globed::api::player::getUsername(remote));
        if (auto icons = globed::api::player::getIcons(remote)) {
            row.iconData = iconDataFor(icons.unwrap());
        }
        row.joinedAt = 0;
        row.lastSeen = nowMs;
        row.messageCount = 0;
        if (!row.displayName.empty()) {
            out.push_back(std::move(row));
        }
    }
#endif
    return out;
}

} // namespace comsplus
