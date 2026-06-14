#pragma once

#include "ChatCore.hpp"

#include <string>

namespace comsplus {

struct ComsPlusSettings {
    bool chatEnabled = true;
    bool privacyEnabled = false;
    std::string fakeName = "ComsPlayer";
    ChatNameMode chatNameMode = ChatNameMode::Auto;
    float chatOpacity = 0.78f;
    int sendCooldownMs = 1500;
    int maxChatMessages = 8;
};

ComsPlusSettings readSettings();
std::string localRealName();
std::int64_t localAccountId();
std::string localIconData();

} // namespace comsplus
