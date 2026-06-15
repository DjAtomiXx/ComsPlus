#include "GlobedBridge.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <set>

#if defined(COMSPLUS_HAS_GLOBED_HEADERS) && __has_include(<globed/soft-link/API.hpp>) && __has_include(<globed/core/Event.hpp>)
    #include <globed/core/Event.hpp>
    #include <globed/soft-link/API.hpp>
    #define COMSPLUS_WITH_GLOBED 1
#else
    #define COMSPLUS_WITH_GLOBED 0
#endif

using namespace geode::prelude;

namespace comsplus {
namespace {

constexpr char const* kEventId = "exploited.comsplus/chat";
constexpr char const* kMinGlobedVersion = "2.1.4";
std::set<std::string> g_seenMessages;

#if COMSPLUS_WITH_GLOBED
struct ComsPlusGlobedEvent : globed::ServerEvent<ComsPlusGlobedEvent, globed::EventServer::Game> {
    static constexpr auto Id = "exploited.comsplus/chat"_spr;

    std::string payload;

    ComsPlusGlobedEvent() = default;
    explicit ComsPlusGlobedEvent(std::string data) : payload(std::move(data)) {}

    std::vector<uint8_t> encode() const {
        return {payload.begin(), payload.end()};
    }

    static geode::Result<ComsPlusGlobedEvent> decode(std::span<const uint8_t> data) {
        if (data.size() > 512) {
            return geode::Err("ComsPlus chat payload is too large");
        }
        return geode::Ok(ComsPlusGlobedEvent(std::string(data.begin(), data.end())));
    }
};

std::optional<geode::ListenerHandle> g_listener;
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
    globed::api::waitForGlobed([this] {
        globed::api::net::registerEvent(kEventId, globed::EventServer::Game);
        g_listener = ComsPlusGlobedEvent::listen([this](ComsPlusGlobedEvent const& event, globed::EventOptions const&) {
            auto decoded = decodePayload(event.payload);
            if (!decoded.has_value()) return;
            if (!g_seenMessages.insert(decoded->messageId).second) return;
            m_received.push_back(std::move(*decoded));
            if (m_received.size() > 30) {
                m_received.erase(m_received.begin(), m_received.begin() + static_cast<long>(m_received.size() - 30));
            }
        });
        log::info("ComsPlus Globed event bridge initialized");
    });
#else
    log::warn("ComsPlus built without Globed headers; network chat is disabled");
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
    if (!globed::api::available()) return "Globed not loaded";
    if (!globed::api::isAtLeast(kMinGlobedVersion)) return "Globed 2.1.4+ required";
    if (!globed::api::net::isConnected()) return "Globed offline";
    if (!globed::api::game::isActive()) return "Join a Globed level";
    return "Globed connected";
#else
    return "Globed bridge unavailable";
#endif
}

bool GlobedBridge::sendChat(ChatMessage const& message) {
#if COMSPLUS_WITH_GLOBED
    if (!isConnected()) return false;
    auto payload = encodePayload(message);
    if (!decodePayload(payload).has_value()) return false;

    g_seenMessages.insert(message.messageId);

    globed::EventOptions options;
    options.server = globed::EventServer::Game;
    options.reliable = true;
    options.urgent = false;
    options.sendBack = true;
    ComsPlusGlobedEvent(std::move(payload)).send(options);
    return true;
#else
    (void)message;
    return false;
#endif
}

std::vector<ChatMessage> GlobedBridge::pollReceived() {
    auto out = std::move(m_received);
    m_received.clear();
    return out;
}

} // namespace comsplus
