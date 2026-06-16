#pragma once

#include "ChatCore.hpp"

#include <string>
#include <vector>

namespace comsplus {

class GlobedBridge {
public:
    static GlobedBridge& get();

    void initialize();
    void maintain();
    bool isAvailable() const;
    bool isConnected() const;
    std::string statusText() const;
    ChatSendResult sendChat(ChatMessage const& message);
    std::vector<ChatMessage> pollReceived();

private:
    GlobedBridge() = default;

    void installListener();
    void registerEvent();
    bool sendNow(ChatMessage const& message);
    void queuePending(ChatMessage const& message);

    bool m_initialized = false;
    bool m_listenerInstalled = false;
    bool m_eventRegistered = false;
    std::vector<ChatMessage> m_pending;
    std::vector<ChatMessage> m_received;
};

} // namespace comsplus
