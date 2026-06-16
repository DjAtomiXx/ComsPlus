#pragma once

#include "ChatCore.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

namespace comsplus {

struct ComsPlusSettings {
    bool chatEnabled = true;
    bool privacyEnabled = false;
    std::string fakeName = "ComsPlayer";
    ChatNameMode chatNameMode = ChatNameMode::Auto;
    float chatOpacity = 0.78f;
    ChatColorMode ownMessageColorMode = ChatColorMode::Default;
    cocos2d::ccColor3B ownMessagePrimaryColor = {115, 255, 214};
    cocos2d::ccColor3B ownMessageSecondaryColor = {255, 79, 125};
    float desktopPanelWidth = 460.0f;
    float desktopPanelHeight = 280.0f;
    float bubbleOpacity = 0.92f;
    float bubbleSize = 46.0f;
    bool hideBubbleInMainMenu = false;
    bool mainMenuChatEnabled = true;
    int sendCooldownMs = 1500;
    int maxChatMessages = 14;
};

ComsPlusSettings readSettings();
std::string localRealName();
std::vector<std::string> localRealNameCandidates();
std::int64_t localAccountId();
std::string localIconData();

} // namespace comsplus
