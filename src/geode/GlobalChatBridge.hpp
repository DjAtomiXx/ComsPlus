#pragma once

#include "ChatCore.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace comsplus {

class GlobalChatBridge {
public:
    static GlobalChatBridge& get();

    void maintain();
    std::string statusText() const;
    ChatSendResult sendChat(ChatMessage const& message);
    std::vector<ChatMessage> pollReceived();

private:
    GlobalChatBridge() = default;

    void startPoll(std::string serverUrl, std::int64_t since);
    void startSend(std::string serverUrl, ChatMessage message);

    mutable std::mutex m_mutex;
    std::vector<ChatMessage> m_received;
    std::int64_t m_lastSeen = 0;
    std::int64_t m_lastPollMs = 0;
    bool m_polling = false;
    bool m_sending = false;
    std::string m_status = "Main chat connecting";
};

} // namespace comsplus
