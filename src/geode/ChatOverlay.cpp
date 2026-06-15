#include "ChatOverlay.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>
#include <sstream>

using namespace geode::prelude;

namespace comsplus {
namespace {

constexpr float kDragThreshold = 6.0f;
ComsPlusChatOverlay* g_activeOverlay = nullptr;

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

bool isMobileLayout() {
#ifdef GEODE_IS_ANDROID
    return true;
#else
    return false;
#endif
}

CCSize winSize() {
    return CCDirector::sharedDirector()->getWinSize();
}

CCSize panelSize() {
    auto win = winSize();
    return {
        std::min(336.0f, win.width - 24.0f),
        std::min(188.0f, win.height - 34.0f)
    };
}

bool pointInRect(CCPoint const& point, CCPoint const& origin, CCSize const& size) {
    return point.x >= origin.x && point.x <= origin.x + size.width &&
        point.y >= origin.y && point.y <= origin.y + size.height;
}

float distance(CCPoint const& a, CCPoint const& b) {
    auto dx = a.x - b.x;
    auto dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

CCPoint add(CCPoint const& a, CCPoint const& b) {
    return {a.x + b.x, a.y + b.y};
}

CCPoint subtract(CCPoint const& a, CCPoint const& b) {
    return {a.x - b.x, a.y - b.y};
}

CCLayerColor* rect(ccColor4B color, CCSize size, CCPoint position) {
    auto layer = CCLayerColor::create(color, size.width, size.height);
    layer->setPosition(position);
    return layer;
}

void addBorder(CCNode* parent, CCSize size, ccColor4B color, float thickness) {
    parent->addChild(rect(color, {size.width, thickness}, {0.0f, size.height - thickness}));
    parent->addChild(rect(color, {size.width, thickness}, {0.0f, 0.0f}));
    parent->addChild(rect(color, {thickness, size.height}, {0.0f, 0.0f}));
    parent->addChild(rect(color, {thickness, size.height}, {size.width - thickness, 0.0f}));
}

CCSprite* createBubbleIcon() {
    auto logoPath = Mod::get()->getTempDir() / "logo.png";
    if (std::filesystem::exists(logoPath)) {
        auto path = utils::string::pathToString(logoPath);
        if (auto sprite = CCSprite::create(path.c_str())) {
            return sprite;
        }
    }
    return CCSprite::create("comsplus-icon.png"_spr);
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

CCPoint defaultBubblePosition() {
    auto win = winSize();
    auto size = readSettings().bubbleSize;
    return {win.width - size - 18.0f, 18.0f};
}

CCPoint defaultPanelPosition() {
    auto win = winSize();
    auto size = panelSize();
    return {(win.width - size.width) * 0.5f, (win.height - size.height) * 0.5f};
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
    auto win = winSize();
    this->setID("comsplus-chat-overlay"_spr);
    this->setAnchorPoint({0.0f, 0.0f});
    this->setContentSize(win);
    this->setPosition({0.0f, 0.0f});
    this->setZOrder(100000);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(-650);
    this->setTouchEnabled(settings.chatEnabled);

    m_bubblePosition = clampedBubblePosition(defaultBubblePosition());
    m_panelPosition = clampedPanelPosition(defaultPanelPosition());

    if (isMobileLayout()) {
        buildBubble();
    }
    buildPanel();
    setExpanded(false);

    if (!settings.chatEnabled) {
        this->setVisible(false);
        return true;
    }

    GlobedBridge::get().initialize();
    m_rateLimiter = std::make_unique<RateLimiter>(settings.sendCooldownMs);
    this->schedule(schedule_selector(ComsPlusChatOverlay::tick), 0.25f);
    g_activeOverlay = this;
    this->rebuild();
    return true;
}

void ComsPlusChatOverlay::onExit() {
    if (g_activeOverlay == this) {
        g_activeOverlay = nullptr;
    }
    CCLayer::onExit();
}

void ComsPlusChatOverlay::buildBubble() {
    if (m_bubbleRoot) {
        m_bubbleRoot->removeFromParentAndCleanup(true);
    }

    auto settings = readSettings();
    auto bubbleOpacity = std::clamp(settings.bubbleOpacity, 0.25f, 1.0f);
    auto bubbleSize = std::clamp(settings.bubbleSize, 34.0f, 72.0f);
    m_lastBubbleOpacity = bubbleOpacity;
    m_lastBubbleSize = bubbleSize;
    auto alpha = [bubbleOpacity](float value) {
        return static_cast<GLubyte>(std::clamp(value * bubbleOpacity, 0.0f, 255.0f));
    };

    m_bubbleRoot = CCNode::create();
    m_bubbleRoot->setID("comsplus-chat-bubble"_spr);
    m_bubbleRoot->setContentSize({bubbleSize, bubbleSize});
    this->addChild(m_bubbleRoot, 4);

    m_bubbleRoot->addChild(rect({190, 10, 28, alpha(92.0f)}, {bubbleSize + 8.0f, bubbleSize + 8.0f}, {-4.0f, -4.0f}));
    m_bubbleRoot->addChild(rect({8, 9, 14, alpha(215.0f)}, {bubbleSize, bubbleSize}, {0.0f, 0.0f}));
    addBorder(m_bubbleRoot, {bubbleSize, bubbleSize}, {255, 42, 62, alpha(235.0f)}, 2.0f);

    if (auto icon = createBubbleIcon()) {
        icon->setID("comsplus-bubble-icon"_spr);
        icon->setAnchorPoint({0.5f, 0.5f});
        icon->setPosition({bubbleSize * 0.5f, bubbleSize * 0.5f});
        icon->setOpacity(alpha(255.0f));
        auto size = icon->getContentSize();
        auto maxSize = std::max(size.width, size.height);
        if (maxSize > 0.0f) {
            icon->setScale((bubbleSize - 5.0f) / maxSize);
        }
        m_bubbleRoot->addChild(icon);
    } else {
        auto dot = CCLabelBMFont::create("...", "bigFont.fnt");
        dot->setAnchorPoint({0.5f, 0.5f});
        dot->setScale(0.55f);
        dot->setColor({255, 240, 242});
        dot->setOpacity(alpha(255.0f));
        dot->setPosition({bubbleSize * 0.5f, bubbleSize * 0.53f});
        m_bubbleRoot->addChild(dot);
    }
}

void ComsPlusChatOverlay::buildPanel() {
    if (m_panelRoot) {
        m_panelRoot->removeFromParentAndCleanup(true);
    }

    auto size = panelSize();
    auto settings = readSettings();
    auto bgAlpha = static_cast<GLubyte>(std::clamp(settings.chatOpacity, 0.2f, 1.0f) * 228.0f);

    m_panelRoot = CCNode::create();
    m_panelRoot->setID("comsplus-chat-panel"_spr);
    m_panelRoot->setContentSize(size);
    this->addChild(m_panelRoot, 5);

    m_panelRoot->addChild(rect({230, 20, 36, 56}, {size.width + 12.0f, size.height + 12.0f}, {-6.0f, -6.0f}));
    m_panelRoot->addChild(rect({7, 8, 13, bgAlpha}, size, {0.0f, 0.0f}));
    m_panelRoot->addChild(rect({80, 7, 15, 92}, {size.width, 30.0f}, {0.0f, size.height - 30.0f}));
    addBorder(m_panelRoot, size, {255, 40, 60, 235}, 2.0f);
    auto innerBorder = CCNode::create();
    innerBorder->setPosition({4.0f, 4.0f});
    addBorder(innerBorder, {size.width - 8.0f, size.height - 8.0f}, {155, 12, 27, 120}, 1.0f);
    m_panelRoot->addChild(innerBorder);

    auto title = CCLabelBMFont::create("ComsPlus", "bigFont.fnt");
    title->setAnchorPoint({0.0f, 0.5f});
    title->setScale(0.38f);
    title->setColor({255, 230, 232});
    title->setPosition({12.0f, size.height - 15.0f});
    m_panelRoot->addChild(title);

    m_status = CCLabelBMFont::create("ComsPlus", "chatFont.fnt");
    m_status->setID("comsplus-status"_spr);
    m_status->setAnchorPoint({1.0f, 0.5f});
    m_status->setScale(0.34f);
    m_status->setColor({255, 138, 148});
    m_status->setPosition({size.width - 12.0f, size.height - 15.0f});
    m_panelRoot->addChild(m_status);

    m_messageRoot = CCNode::create();
    m_messageRoot->setID("comsplus-messages"_spr);
    m_messageRoot->setContentSize({size.width - 20.0f, size.height - 80.0f});
    m_messageRoot->setPosition({10.0f, 42.0f});
    m_panelRoot->addChild(m_messageRoot);

    m_input = TextInput::create(size.width - 96.0f, "Message", "chatFont.fnt");
    m_input->setID("comsplus-input"_spr);
    m_input->setScale(0.62f);
    m_input->setMaxCharCount(120);
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setPosition({(size.width - 70.0f) * 0.5f, 20.0f});
    m_panelRoot->addChild(m_input);

    auto sendSprite = ButtonSprite::create("Send", "chatFont.fnt", "GJ_button_06.png", 0.54f);
    auto sendButton = CCMenuItemSpriteExtra::create(sendSprite, this, menu_selector(ComsPlusChatOverlay::onSend));
    sendButton->setID("comsplus-send-button"_spr);
    auto menu = CCMenu::create();
    menu->setID("comsplus-send-menu"_spr);
    menu->setPosition({size.width - 38.0f, 20.0f});
    menu->addChild(sendButton);
    m_panelRoot->addChild(menu);
}

void ComsPlusChatOverlay::updateLayout() {
    auto win = winSize();
    this->setContentSize(win);

    if (m_bubbleRoot) {
        m_bubblePosition = clampedBubblePosition(m_bubblePosition);
        m_bubbleRoot->setPosition(m_bubblePosition);
    }

    if (m_panelRoot) {
        if (isMobileLayout()) {
            m_panelPosition = defaultPanelPosition();
        }
        m_panelPosition = clampedPanelPosition(m_panelPosition);
        m_panelRoot->setPosition(m_panelPosition);
    }
}

void ComsPlusChatOverlay::setExpanded(bool expanded) {
    m_expanded = expanded;
    if (m_bubbleRoot) m_bubbleRoot->setVisible(isMobileLayout() && !expanded);
    if (m_panelRoot) m_panelRoot->setVisible(expanded);
    updateLayout();
}

void ComsPlusChatOverlay::openPanel() {
    setExpanded(true);
    if (auto gm = GameManager::get()) {
        if (gm->m_playLayer) {
            gm->m_playLayer->pauseGame(false);
        }
    }
    raiseToScene();
}

void ComsPlusChatOverlay::collapse() {
    setExpanded(false);
}

void ComsPlusChatOverlay::togglePanel() {
    if (m_expanded) {
        collapse();
    } else {
        openPanel();
    }
}

void ComsPlusChatOverlay::raiseToScene() {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene || this->getParent() == scene) {
        this->setZOrder(100000);
        return;
    }

    this->retain();
    this->removeFromParentAndCleanup(false);
    scene->addChild(this, 100000);
    this->release();
}

bool ComsPlusChatOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!this->isVisible()) return false;

    auto point = touch->getLocation();
    m_touchStart = point;
    m_dragged = false;

    if (!m_expanded && pointInBubble(point)) {
        m_dragMode = DragMode::Bubble;
        m_dragStart = m_bubblePosition;
        return true;
    }

    if (m_expanded && !isMobileLayout() && pointInPanelHeader(point)) {
        m_dragMode = DragMode::Panel;
        m_dragStart = m_panelPosition;
        return true;
    }

    m_dragMode = DragMode::None;
    return false;
}

void ComsPlusChatOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_dragMode == DragMode::None) return;

    auto point = touch->getLocation();
    auto delta = subtract(point, m_touchStart);
    if (distance(point, m_touchStart) > kDragThreshold) {
        m_dragged = true;
    }

    if (m_dragMode == DragMode::Bubble) {
        m_bubblePosition = clampedBubblePosition(add(m_dragStart, delta));
    } else if (m_dragMode == DragMode::Panel && !isMobileLayout()) {
        m_panelPosition = clampedPanelPosition(add(m_dragStart, delta));
    }
    updateLayout();
}

void ComsPlusChatOverlay::ccTouchEnded(CCTouch*, CCEvent*) {
    if (m_dragMode == DragMode::Bubble && !m_dragged) {
        openPanel();
    }
    m_dragMode = DragMode::None;
}

void ComsPlusChatOverlay::tick(float dt) {
    m_elapsed += dt;
    if (m_elapsed < 0.25f) return;
    m_elapsed = 0.0f;

    if (m_status) {
        m_status->setString(GlobedBridge::get().statusText().c_str());
    }

    updateLayout();

    if (isMobileLayout() && m_bubbleRoot) {
        auto settings = readSettings();
        auto bubbleSize = std::clamp(settings.bubbleSize, 34.0f, 72.0f);
        auto bubbleOpacity = std::clamp(settings.bubbleOpacity, 0.25f, 1.0f);
        if (std::abs(bubbleSize - m_lastBubbleSize) > 0.5f || std::abs(bubbleOpacity - m_lastBubbleOpacity) > 0.01f) {
            buildBubble();
            updateLayout();
        }
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

    auto rootSize = m_messageRoot->getContentSize();
    float y = rootSize.height - 10.0f;
    for (auto const& rendered : m_messages) {
        auto row = rect(
            rendered.local ? ccColor4B{35, 80, 68, 80} : ccColor4B{32, 38, 56, 72},
            {rootSize.width, 18.0f},
            {0.0f, y - 9.0f}
        );
        m_messageRoot->addChild(row);

        auto icon = createIconNode(rendered.message.iconData);
        icon->setAnchorPoint({0.0f, 0.5f});
        icon->setPosition({4.0f, y});
        m_messageRoot->addChild(icon);

        auto name = CCLabelBMFont::create((rendered.message.displayName + ":").c_str(), "chatFont.fnt");
        name->setAnchorPoint({0.0f, 0.5f});
        name->setScale(0.34f);
        name->setColor(rendered.local ? ccColor3B{115, 255, 214} : ccColor3B{255, 160, 168});
        name->setPosition({28.0f, y});
        name->limitLabelWidth(84.0f, 0.34f, 0.08f);
        m_messageRoot->addChild(name);

        auto text = CCLabelBMFont::create(rendered.message.text.c_str(), "chatFont.fnt");
        text->setAnchorPoint({0.0f, 0.5f});
        text->setScale(0.31f);
        text->setColor({238, 240, 246});
        text->setPosition({116.0f, y});
        text->setWidth(std::max(110.0f, rootSize.width - 122.0f));
        m_messageRoot->addChild(text);

        y -= 20.0f;
    }
}

bool ComsPlusChatOverlay::pointInBubble(CCPoint const& point) const {
    return m_bubbleRoot && pointInRect(point, m_bubblePosition, m_bubbleRoot->getContentSize());
}

bool ComsPlusChatOverlay::pointInPanelHeader(CCPoint const& point) const {
    if (!m_panelRoot) return false;
    auto size = m_panelRoot->getContentSize();
    return pointInRect(
        point,
        {m_panelPosition.x, m_panelPosition.y + size.height - 32.0f},
        {size.width, 32.0f}
    );
}

CCPoint ComsPlusChatOverlay::clampedBubblePosition(CCPoint position) const {
    auto win = winSize();
    auto size = m_bubbleRoot ? m_bubbleRoot->getContentSize().width : readSettings().bubbleSize;
    return {
        std::clamp(position.x, 8.0f, std::max(8.0f, win.width - size - 8.0f)),
        std::clamp(position.y, 8.0f, std::max(8.0f, win.height - size - 8.0f))
    };
}

CCPoint ComsPlusChatOverlay::clampedPanelPosition(CCPoint position) const {
    auto win = winSize();
    auto size = panelSize();
    return {
        std::clamp(position.x, 8.0f, std::max(8.0f, win.width - size.width - 8.0f)),
        std::clamp(position.y, 8.0f, std::max(8.0f, win.height - size.height - 8.0f))
    };
}

ComsPlusChatOverlay* activeChatOverlay() {
    return g_activeOverlay;
}

void collapseActiveChatOverlay() {
    if (g_activeOverlay) {
        g_activeOverlay->collapse();
    }
}

void toggleActiveChatOverlay() {
    if (g_activeOverlay) {
        g_activeOverlay->togglePanel();
    }
}

void raiseActiveChatOverlay() {
    if (g_activeOverlay) {
        g_activeOverlay->raiseToScene();
    }
}

} // namespace comsplus
