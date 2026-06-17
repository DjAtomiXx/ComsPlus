#pragma once

#include "ChatCore.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace comsplus {

class GlobedBridge {
public:
    static GlobedBridge& get();

    void initialize();
    void maintain();
    void shutdown();
    bool isAvailable() const;
    bool isConnected() const;
    std::string statusText() const;
    ChatSendResult sendChat(ChatMessage const& message);
    std::vector<ChatMessage> pollReceived();
    std::vector<ChatPresence> presenceSnapshot() const;

private:
    GlobedBridge() = default;

    void installListener();
    void uninstallListener();
    void registerEvent();
    bool handleLevelDataMessage(void* message);
    bool sendNow(ChatMessage const& message);
    void queuePending(ChatMessage const& message);

    mutable std::mutex m_mutex;
    bool m_initialized = false;
    bool m_listenerInstalled = false;
    bool m_eventRegistered = false;
    std::int64_t m_lastSendMs = 0;
    std::vector<ChatMessage> m_pending;
    std::vector<ChatMessage> m_received;
};

} // namespace comsplus
