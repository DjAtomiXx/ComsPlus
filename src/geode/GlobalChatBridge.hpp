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
    std::vector<ChatPresence> presenceSnapshot() const;
    std::vector<ChatActivity> activitySnapshot() const;

private:
    GlobalChatBridge() = default;

    void startPoll(std::string serverUrl, std::int64_t since);
    void startMeta(std::string serverUrl);
    void startSend(std::string serverUrl, ChatMessage message);

    mutable std::mutex m_mutex;
    std::vector<ChatMessage> m_received;
    std::vector<ChatPresence> m_presence;
    std::vector<ChatActivity> m_activity;
    std::int64_t m_lastSeen = 0;
    std::int64_t m_lastPollMs = 0;
    std::int64_t m_lastMetaMs = 0;
    bool m_polling = false;
    bool m_metaLoading = false;
    bool m_sending = false;
    std::string m_status = "Main chat connecting";
};

} // namespace comsplus
