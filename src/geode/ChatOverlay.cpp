#include "ChatOverlay.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/ui/TextInput.hpp>

#include <chrono>
#include <random>
#include <sstream>

using namespace geode::prelude;

namespace comsplus {
namespace {

std::int64_t nowMs() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string randomMessageId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream out;
    out << std::hex << nowMs() << "-" << rng();
    return out.str();
}

int parseIconPart(std::string const& iconData, std::string const& key, int fallback) {
    auto needle = key + ":";
    auto pos = iconData.find(needle);
    if (pos == std::string::npos) return fallback;
    pos += needle.size();
    auto end = iconData.find(':', pos);
    auto text = iconData.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    return numFromString<int>(text).unwrapOr(fallback);
}

ccColor3B colorFor(int colorId) {
    auto gm = GameManager::get();
    if (!gm || colorId < 0) return ccWHITE;
    return gm->colorForIdx(colorId);
}

} // namespace

ComsPlusChatOverlay* ComsPlusChatOverlay::create() {
    auto ret = new ComsPlusChatOverlay();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ComsPlusChatOverlay::init() {
    if (!CCLayer::init()) return false;

    auto settings = readSettings();
    if (!settings.chatEnabled) {
        this->setVisible(false);
        return true;
    }

    auto winSize = CCDirector::sharedDirector()->getWinSize();
    this->setID("comsplus-chat-overlay"_spr);
    this->setAnchorPoint({0.0f, 0.0f});
    this->setContentSize({300.0f, 156.0f});
    this->setPosition({12.0f, winSize.height - 170.0f});
    this->setZOrder(999);

    m_background = CCLayerColor::create({8, 12, 24, static_cast<GLubyte>(settings.chatOpacity * 210.0f)}, 300.0f, 156.0f);
    m_background->setID("comsplus-chat-bg"_spr);
    this->addChild(m_background);

    m_status = CCLabelBMFont::create("ComsPlus", "chatFont.fnt");
    m_status->setID("comsplus-status"_spr);
    m_status->setAnchorPoint({0.0f, 0.5f});
    m_status->setScale(0.42f);
    m_status->setPosition({10.0f, 142.0f});
    this->addChild(m_status);

    m_messageRoot = CCNode::create();
    m_messageRoot->setID("comsplus-messages"_spr);
    m_messageRoot->setContentSize({280.0f, 104.0f});
    m_messageRoot->setPosition({10.0f, 32.0f});
    this->addChild(m_messageRoot);

    m_input = TextInput::create(220.0f, "ComsPlus message", "chatFont.fnt");
    m_input->setID("comsplus-input"_spr);
    m_input->setScale(0.62f);
    m_input->setMaxCharCount(120);
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setPosition({116.0f, 16.0f});
    this->addChild(m_input);

    auto sendSprite = ButtonSprite::create("Send", "chatFont.fnt", "GJ_button_01.png", 0.58f);
    auto sendButton = CCMenuItemSpriteExtra::create(sendSprite, this, menu_selector(ComsPlusChatOverlay::onSend));
    sendButton->setID("comsplus-send-button"_spr);
    auto menu = CCMenu::create();
    menu->setID("comsplus-send-menu"_spr);
    menu->setPosition({260.0f, 16.0f});
    menu->addChild(sendButton);
    this->addChild(menu);

    GlobedBridge::get().initialize();
    m_rateLimiter = std::make_unique<RateLimiter>(settings.sendCooldownMs);
    this->schedule(schedule_selector(ComsPlusChatOverlay::tick), 0.25f);
    this->rebuild();
    return true;
}

void ComsPlusChatOverlay::tick(float dt) {
    m_elapsed += dt;
    if (m_elapsed < 0.25f) return;
    m_elapsed = 0.0f;

    if (m_status) {
        m_status->setString(GlobedBridge::get().statusText().c_str());
    }

    auto received = GlobedBridge::get().pollReceived();
    for (auto& message : received) {
        appendMessage(std::move(message), false);
    }
}

void ComsPlusChatOverlay::onSend(CCObject*) {
    if (!m_input) return;

    auto text = sanitizeMessage(std::string(m_input->getString()));
    if (text.empty()) return;

    auto current = nowMs();
    if (m_rateLimiter && !m_rateLimiter->canSend(current)) {
        if (m_status) m_status->setString("Slow down");
        return;
    }

    auto message = makeLocalMessage(text);
    if (!GlobedBridge::get().sendChat(message)) {
        if (m_status) m_status->setString(GlobedBridge::get().statusText().c_str());
        return;
    }

    if (m_rateLimiter) m_rateLimiter->markSent(current);
    m_input->setString("", false);
    appendMessage(std::move(message), true);
}

void ComsPlusChatOverlay::appendMessage(ChatMessage message, bool local) {
    auto settings = readSettings();
    m_messages.push_back({std::move(message), local});
    while (static_cast<int>(m_messages.size()) > settings.maxChatMessages) {
        m_messages.pop_front();
    }
    rebuild();
}

ChatMessage ComsPlusChatOverlay::makeLocalMessage(std::string text) const {
    auto settings = readSettings();
    DisplayNameSettings displaySettings{
        settings.privacyEnabled,
        settings.fakeName,
        settings.chatNameMode
    };

    ChatMessage message;
    message.protocolVersion = 1;
    message.messageId = randomMessageId();
    message.accountId = localAccountId();
    message.displayName = selectDisplayName(localRealName(), displaySettings);
    message.iconData = localIconData();
    message.text = sanitizeMessage(text);
    message.timestamp = nowMs();
    return message;
}

CCNode* ComsPlusChatOverlay::createIconNode(std::string const& iconData) const {
    auto cube = parseIconPart(iconData, "cube", 1);
    auto color1 = parseIconPart(iconData, "c1", 0);
    auto color2 = parseIconPart(iconData, "c2", 3);

    auto icon = SimplePlayer::create(cube);
    if (icon) {
        icon->setColor(colorFor(color1));
        icon->setSecondColor(colorFor(color2));
        icon->setScale(0.22f);
        return icon;
    }

    auto fallback = CCLabelBMFont::create("[]", "chatFont.fnt");
    fallback->setScale(0.4f);
    return fallback;
}

void ComsPlusChatOverlay::rebuild() {
    if (!m_messageRoot) return;
    m_messageRoot->removeAllChildrenWithCleanup(true);

    float y = 96.0f;
    for (auto const& rendered : m_messages) {
        auto icon = createIconNode(rendered.message.iconData);
        icon->setAnchorPoint({0.0f, 0.5f});
        icon->setPosition({0.0f, y});
        m_messageRoot->addChild(icon);

        auto name = CCLabelBMFont::create((rendered.message.displayName + ":").c_str(), "chatFont.fnt");
        name->setAnchorPoint({0.0f, 0.5f});
        name->setScale(0.34f);
        name->setColor(rendered.local ? ccColor3B{110, 255, 210} : ccColor3B{130, 190, 255});
        name->setPosition({24.0f, y});
        m_messageRoot->addChild(name);

        auto text = CCLabelBMFont::create(rendered.message.text.c_str(), "chatFont.fnt");
        text->setAnchorPoint({0.0f, 0.5f});
        text->setScale(0.32f);
        text->setPosition({24.0f + std::min<float>(86.0f, rendered.message.displayName.size() * 6.0f), y});
        text->setWidth(176.0f);
        m_messageRoot->addChild(text);

        y -= 13.0f;
    }
}

} // namespace comsplus
