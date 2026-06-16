#include "ComsPlusSettings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/Bindings.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

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

ChatColorMode parseColorModeSetting(std::string mode) {
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (mode == "custom") return ChatColorMode::Custom;
    if (mode == "rainbow") return ChatColorMode::Rainbow;
    return ChatColorMode::Default;
}

template <typename T>
T settingOr(std::string_view key, T fallback) {
    return Mod::get()->getSettingValue<T>(key);
}

void addNameCandidate(std::vector<std::string>& names, std::string name) {
    auto clean = sanitizeName(name);
    if (clean.empty()) return;
    if (std::find(names.begin(), names.end(), clean) != names.end()) return;
    names.push_back(std::move(clean));
}

} // namespace

ComsPlusSettings readSettings() {
    ComsPlusSettings settings;
    settings.chatEnabled = settingOr<bool>("chat-enabled", settings.chatEnabled);
    settings.privacyEnabled = settingOr<bool>("privacy-enabled", settings.privacyEnabled);
    settings.fakeName = settingOr<std::string>("fake-name", settings.fakeName);
    settings.chatNameMode = parseMode(settingOr<std::string>("chat-name-mode", "auto"));
    settings.chatOpacity = static_cast<float>(settingOr<double>("chat-opacity", settings.chatOpacity));
    settings.ownMessageColorMode = parseColorModeSetting(settingOr<std::string>("own-message-color-mode", "default"));
    settings.ownMessagePrimaryColor = settingOr<ccColor3B>("own-message-primary-color", settings.ownMessagePrimaryColor);
    settings.ownMessageSecondaryColor = settingOr<ccColor3B>("own-message-secondary-color", settings.ownMessageSecondaryColor);
    settings.desktopPanelWidth = static_cast<float>(settingOr<double>("desktop-panel-width", settings.desktopPanelWidth));
    settings.desktopPanelHeight = static_cast<float>(settingOr<double>("desktop-panel-height", settings.desktopPanelHeight));
    settings.bubbleOpacity = static_cast<float>(settingOr<double>("bubble-opacity", settings.bubbleOpacity));
    settings.bubbleSize = static_cast<float>(settingOr<double>("bubble-size", settings.bubbleSize));
    settings.hideBubbleInMainMenu = settingOr<bool>("hide-bubble-main-menu", settings.hideBubbleInMainMenu);
    settings.mainMenuChatEnabled = settingOr<bool>("main-menu-chat-enabled", settings.mainMenuChatEnabled);
    settings.sendCooldownMs = static_cast<int>(settingOr<int64_t>("send-cooldown-ms", settings.sendCooldownMs));
    settings.maxChatMessages = static_cast<int>(settingOr<int64_t>("max-chat-messages", settings.maxChatMessages));
    settings.chatOpacity = std::clamp(settings.chatOpacity, 0.2f, 1.0f);
    settings.desktopPanelWidth = std::clamp(settings.desktopPanelWidth, 320.0f, 580.0f);
    settings.desktopPanelHeight = std::clamp(settings.desktopPanelHeight, 190.0f, 340.0f);
    settings.bubbleOpacity = std::clamp(settings.bubbleOpacity, 0.25f, 1.0f);
    settings.bubbleSize = std::clamp(settings.bubbleSize, 26.0f, 72.0f);
    settings.sendCooldownMs = std::clamp(settings.sendCooldownMs, 500, 10000);
    settings.maxChatMessages = std::clamp(settings.maxChatMessages, 4, 32);
    return settings;
}

std::string localRealName() {
    auto candidates = localRealNameCandidates();
    if (!candidates.empty()) return candidates.front();
    return "Player";
}

std::vector<std::string> localRealNameCandidates() {
    std::vector<std::string> names;

    if (auto account = GJAccountManager::get()) {
        addNameCandidate(names, std::string(account->m_username));
    }

    auto gm = GameManager::get();
    if (gm) {
        addNameCandidate(names, std::string(gm->m_playerName));
    }

    return names;
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
