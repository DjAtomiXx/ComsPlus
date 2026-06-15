#include "ComsPlusSettings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/Bindings.hpp>

#include <algorithm>
#include <sstream>

using namespace geode::prelude;

namespace comsplus {
namespace {

ChatNameMode parseMode(std::string mode) {
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (mode == "real") return ChatNameMode::Real;
    if (mode == "fake") return ChatNameMode::Fake;
    return ChatNameMode::Auto;
}

template <typename T>
T settingOr(std::string_view key, T fallback) {
    return Mod::get()->getSettingValue<T>(key);
}

} // namespace

ComsPlusSettings readSettings() {
    ComsPlusSettings settings;
    settings.chatEnabled = settingOr<bool>("chat-enabled", settings.chatEnabled);
    settings.privacyEnabled = settingOr<bool>("privacy-enabled", settings.privacyEnabled);
    settings.fakeName = settingOr<std::string>("fake-name", settings.fakeName);
    settings.chatNameMode = parseMode(settingOr<std::string>("chat-name-mode", "auto"));
    settings.chatOpacity = static_cast<float>(settingOr<double>("chat-opacity", settings.chatOpacity));
    settings.bubbleOpacity = static_cast<float>(settingOr<double>("bubble-opacity", settings.bubbleOpacity));
    settings.bubbleSize = static_cast<float>(settingOr<double>("bubble-size", settings.bubbleSize));
    settings.hideBubbleInMainMenu = settingOr<bool>("hide-bubble-main-menu", settings.hideBubbleInMainMenu);
    settings.sendCooldownMs = static_cast<int>(settingOr<int64_t>("send-cooldown-ms", settings.sendCooldownMs));
    settings.maxChatMessages = static_cast<int>(settingOr<int64_t>("max-chat-messages", settings.maxChatMessages));
    settings.chatOpacity = std::clamp(settings.chatOpacity, 0.2f, 1.0f);
    settings.bubbleOpacity = std::clamp(settings.bubbleOpacity, 0.25f, 1.0f);
    settings.bubbleSize = std::clamp(settings.bubbleSize, 34.0f, 72.0f);
    settings.sendCooldownMs = std::clamp(settings.sendCooldownMs, 500, 10000);
    settings.maxChatMessages = std::clamp(settings.maxChatMessages, 3, 20);
    return settings;
}

std::string localRealName() {
    auto gm = GameManager::get();
    if (!gm) return "Player";
    auto name = std::string(gm->m_playerName);
    return name.empty() ? "Player" : name;
}

std::int64_t localAccountId() {
    auto account = GJAccountManager::get();
    return account ? static_cast<std::int64_t>(account->m_accountID) : 0;
}

std::string localIconData() {
    auto gm = GameManager::get();
    if (!gm) return "cube:1:c1:0:c2:3:glow:-1";

    std::ostringstream out;
    out << "cube:" << gm->m_playerFrame
        << ":c1:" << gm->m_playerColor
        << ":c2:" << gm->m_playerColor2
        << ":glow:" << (gm->m_playerGlow ? static_cast<int>(gm->m_playerGlowColor.value()) : -1);
    return out.str();
}

} // namespace comsplus
