#pragma once

#include "ChatCore.hpp"

#include <string>
#include <vector>

namespace comsplus {

struct ComsPlusSettings {
    bool chatEnabled = true;
    bool privacyEnabled = false;
    std::string fakeName = "ComsPlayer";
    ChatNameMode chatNameMode = ChatNameMode::Auto;
    float chatOpacity = 0.78f;
    float bubbleOpacity = 0.92f;
    float bubbleSize = 46.0f;
    bool hideBubbleInMainMenu = true;
    int sendCooldownMs = 1500;
    int maxChatMessages = 8;
};

ComsPlusSettings readSettings();
std::string localRealName();
std::vector<std::string> localRealNameCandidates();
std::int64_t localAccountId();
std::string localIconData();

} // namespace comsplus
