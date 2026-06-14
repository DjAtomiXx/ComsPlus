#pragma once

#include "ChatCore.hpp"

#include <string>
#include <vector>

namespace comsplus {

class GlobedBridge {
public:
    static GlobedBridge& get();

    void initialize();
    bool isAvailable() const;
    bool isConnected() const;
    std::string statusText() const;
    bool sendChat(ChatMessage const& message);
    std::vector<ChatMessage> pollReceived();

private:
    GlobedBridge() = default;

    bool m_initialized = false;
    std::vector<ChatMessage> m_received;
};

} // namespace comsplus
