#include "ChatOverlay.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <optional>
#include <random>
#include <sstream>
#include <string_view>
#include <vector>

using namespace geode::prelude;

namespace comsplus {
namespace {

constexpr float kDragThreshold = 6.0f;
constexpr int kOverlayZOrder = 1000000;
constexpr int kOverlayTouchPriority = -650;
constexpr std::int64_t kDefaultTempBanMs = 60 * 60 * 1000;
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

std::string lowercaseAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

bool namesEqual(std::string const& left, std::string const& right) {
    return lowercaseAscii(sanitizeName(left)) == lowercaseAscii(sanitizeName(right));
}

bool isDevName(std::string const& name) {
    return namesEqual(name, "Lunastry");
}

bool isLocalDevUser() {
    auto candidates = localRealNameCandidates();
    return std::any_of(candidates.begin(), candidates.end(), [](std::string const& name) {
        return isDevName(name);
    });
}

bool hasDevBadge(ChatMessage const& message) {
    return message.authorRole == ChatAuthorRole::Dev || isDevName(message.displayName);
}

std::vector<std::string> parseCommandArgs(std::string_view input) {
    std::vector<std::string> args;
    std::string current;
    bool quoted = false;
    bool escaped = false;

    for (char ch : input) {
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\' && quoted) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && std::isspace(static_cast<unsigned char>(ch)) != 0) {
            if (!current.empty()) {
                args.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }

    if (!current.empty()) {
        args.push_back(std::move(current));
    }
    return args;
}

std::string joinArgs(std::vector<std::string> const& args, std::size_t start) {
    std::string out;
    for (std::size_t i = start; i < args.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += args[i];
    }
    return out;
}

std::optional<std::int64_t> parseDurationMs(std::string const& text) {
    if (text.empty()) return std::nullopt;

    auto unit = text.back();
    auto multiplier = 0LL;
    switch (unit) {
        case 's':
        case 'S':
            multiplier = 1000;
            break;
        case 'm':
        case 'M':
            multiplier = 60 * 1000;
            break;
        case 'h':
        case 'H':
            multiplier = 60 * 60 * 1000;
            break;
        case 'd':
        case 'D':
            multiplier = 24 * 60 * 60 * 1000;
            break;
        default:
            return std::nullopt;
    }

    std::int64_t amount = 0;
    auto number = std::string_view(text).substr(0, text.size() - 1);
    auto result = std::from_chars(number.data(), number.data() + number.size(), amount);
    if (result.ec != std::errc() || amount <= 0) return std::nullopt;
    constexpr auto maxDuration = 30LL * 24 * 60 * 60 * 1000;
    if (amount > maxDuration / multiplier) return maxDuration;
    return std::min<std::int64_t>(amount * multiplier, maxDuration);
}

std::string durationText(std::int64_t durationMs) {
    constexpr std::int64_t second = 1000;
    constexpr std::int64_t minute = 60 * second;
    constexpr std::int64_t hour = 60 * minute;
    constexpr std::int64_t day = 24 * hour;

    if (durationMs % day == 0) return std::to_string(durationMs / day) + "d";
    if (durationMs % hour == 0) return std::to_string(durationMs / hour) + "h";
    if (durationMs % minute == 0) return std::to_string(durationMs / minute) + "m";
    return std::to_string(std::max<std::int64_t>(1, durationMs / second)) + "s";
}

std::string moderationReason(std::string const& text) {
    auto marker = text.find("Reason: ");
    if (marker == std::string::npos) return sanitizeMessage(text);
    return sanitizeMessage(text.substr(marker + 8));
}

bool isMobileLayout() {
#ifdef GEODE_IS_ANDROID
    return true;
#else
    return false;
#endif
}

bool isMainMenuContext() {
    auto gm = GameManager::get();
    return !gm || !gm->m_playLayer;
}

CCSize winSize() {
    return CCDirector::sharedDirector()->getWinSize();
}

CCSize panelSize() {
    auto win = winSize();
    auto settings = readSettings();
    if (!isMobileLayout()) {
        return {
            std::min(settings.desktopPanelWidth, win.width - 24.0f),
            std::min(settings.desktopPanelHeight, win.height - 34.0f)
        };
    }
    return {
        std::min(520.0f, win.width - 12.0f),
        std::min(300.0f, win.height - 16.0f)
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

ccColor4F color4f(ccColor4B color) {
    return {
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f
    };
}

CCDrawNode* disc(ccColor4B fill, float radius, CCPoint center, ccColor4B border = {0, 0, 0, 0}, float borderWidth = 0.0f) {
    auto draw = CCDrawNode::create();
    std::vector<CCPoint> points;
    points.reserve(56);
    for (auto i = 0; i < 56; ++i) {
        auto angle = static_cast<float>(i) * 6.28318530718f / 56.0f;
        points.push_back({
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius
        });
    }
    draw->drawPolygon(points.data(), static_cast<unsigned int>(points.size()), color4f(fill), borderWidth, color4f(border));
    return draw;
}

void addBorder(CCNode* parent, CCSize size, ccColor4B color, float thickness) {
    parent->addChild(rect(color, {size.width, thickness}, {0.0f, size.height - thickness}));
    parent->addChild(rect(color, {size.width, thickness}, {0.0f, 0.0f}));
    parent->addChild(rect(color, {thickness, size.height}, {0.0f, 0.0f}));
    parent->addChild(rect(color, {thickness, size.height}, {size.width - thickness, 0.0f}));
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

std::string currentLevelName() {
    if (auto gm = GameManager::get()) {
        if (gm->m_playLayer && gm->m_playLayer->m_level) {
            auto name = sanitizeName(std::string(gm->m_playLayer->m_level->m_levelName));
            if (!name.empty()) {
                return name;
            }
        }
    }
    return isMainMenuContext() ? "Main Menu" : "this level";
}

std::int64_t currentLevelId() {
    if (auto gm = GameManager::get()) {
        if (gm->m_playLayer && gm->m_playLayer->m_level) {
            return static_cast<std::int64_t>(gm->m_playLayer->m_level->m_levelID);
        }
    }
    return isMainMenuContext() ? -1 : 0;
}

std::string currentLevelKey() {
    return std::to_string(currentLevelId()) + ":" + currentLevelName();
}

std::string colorToWire(ccColor3B color) {
    return geode::cocos::cc3bToHexString(color);
}

ccColor3B colorFromWire(std::string const& value, ccColor3B fallback) {
    auto parsed = geode::cocos::cc3bFromHexString(value, true);
    if (parsed) return parsed.unwrap();
    return fallback;
}

ccColor3B blendColor(ccColor3B a, ccColor3B b, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    auto mix = [amount](GLubyte left, GLubyte right) {
        return static_cast<GLubyte>(std::round(static_cast<float>(left) + (static_cast<float>(right) - static_cast<float>(left)) * amount));
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

ccColor3B rainbowColor(float phase) {
    auto channel = [](float value) {
        return static_cast<GLubyte>(std::round(155.0f + 80.0f * ((std::sin(value) + 1.0f) * 0.5f)));
    };
    return {
        channel(phase),
        channel(phase + 2.0943951f),
        channel(phase + 4.1887902f)
    };
}

ccColor3B messageNameColor(ChatMessage const& message, bool local, float phase) {
    auto fallback = local ? ccColor3B{115, 255, 214} : ccColor3B{255, 160, 168};
    if (message.colorMode == ChatColorMode::Custom) {
        return colorFromWire(message.primaryColor, fallback);
    }
    if (message.colorMode == ChatColorMode::Rainbow) {
        return rainbowColor(phase);
    }
    return fallback;
}

ccColor3B messageBodyColor(ChatMessage const& message, bool local, float phase) {
    auto fallback = local ? ccColor3B{238, 246, 244} : ccColor3B{238, 240, 246};
    if (message.colorMode == ChatColorMode::Custom) {
        return blendColor(colorFromWire(message.secondaryColor, fallback), ccWHITE, 0.28f);
    }
    if (message.colorMode == ChatColorMode::Rainbow) {
        return blendColor(rainbowColor(phase + 0.9f), ccWHITE, 0.42f);
    }
    return fallback;
}

ccColor4B rowBackground(ChatMessage const& message, bool local, float phase) {
    auto accent = messageNameColor(message, local, phase);
    auto base = isMainMenuContext() ?
        (local ? ccColor3B{8, 35, 58} : ccColor3B{12, 22, 45}) :
        (local ? ccColor3B{11, 36, 34} : ccColor3B{29, 15, 23});
    auto mixed = blendColor(base, accent, local ? 0.18f : 0.12f);
    return {mixed.r, mixed.g, mixed.b, local ? static_cast<GLubyte>(94) : static_cast<GLubyte>(78)};
}

ccColor3B panelAccent() {
    return isMainMenuContext() ? ccColor3B{76, 178, 255} : ccColor3B{255, 48, 78};
}

ccColor4B panelGlow(float alpha) {
    auto accent = panelAccent();
    return {accent.r, accent.g, accent.b, static_cast<GLubyte>(alpha)};
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
    this->setZOrder(kOverlayZOrder);
    this->setTouchMode(kCCTouchesOneByOne);
    this->setTouchPriority(kOverlayTouchPriority);
    this->setTouchEnabled(false);

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
    this->setTouchEnabled(true);
    this->schedule(schedule_selector(ComsPlusChatOverlay::tick), 0.25f);
    g_activeOverlay = this;
    this->rebuild();
    return true;
}

void ComsPlusChatOverlay::onEnter() {
    CCLayer::onEnter();
    g_activeOverlay = this;
    this->setTouchPriority(kOverlayTouchPriority);
    this->setTouchEnabled(readSettings().chatEnabled);
    this->scheduleOnce(schedule_selector(ComsPlusChatOverlay::applyTouchPrioritiesDelayed), 0.0f);
}

void ComsPlusChatOverlay::onExit() {
    this->setTouchEnabled(false);
    if (!m_reparenting && g_activeOverlay == this) {
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
    auto bubbleSize = std::clamp(settings.bubbleSize, 26.0f, 72.0f);
    m_lastBubbleOpacity = bubbleOpacity;
    m_lastBubbleSize = bubbleSize;
    auto alpha = [bubbleOpacity](float value) {
        return static_cast<GLubyte>(std::clamp(value * bubbleOpacity, 0.0f, 255.0f));
    };

    m_bubbleRoot = CCNode::create();
    m_bubbleRoot->setID("comsplus-chat-bubble"_spr);
    m_bubbleRoot->setContentSize({bubbleSize, bubbleSize});
    this->addChild(m_bubbleRoot, 4);

    auto accent = panelAccent();
    auto center = CCPoint{bubbleSize * 0.5f, bubbleSize * 0.5f};
    auto ringWidth = std::max(1.2f, bubbleSize * 0.035f);
    m_bubbleRoot->addChild(disc({accent.r, accent.g, accent.b, alpha(42.0f)}, bubbleSize * 0.55f, center));
    m_bubbleRoot->addChild(disc({5, 8, 17, alpha(252.0f)}, bubbleSize * 0.49f, center));
    m_bubbleRoot->addChild(disc({accent.r, accent.g, accent.b, alpha(210.0f)}, bubbleSize * 0.49f, center));
    m_bubbleRoot->addChild(disc({6, 10, 20, alpha(255.0f)}, bubbleSize * 0.49f - ringWidth, center));
    m_bubbleRoot->addChild(disc({13, 23, 37, alpha(255.0f)}, bubbleSize * 0.37f, center));

    auto chat = CCLabelBMFont::create("C+", "bigFont.fnt");
    chat->setID("comsplus-bubble-glyph"_spr);
    chat->setAnchorPoint({0.5f, 0.5f});
    chat->setScale(std::clamp(bubbleSize / 92.0f, 0.28f, 0.62f));
    chat->setColor({218, 248, 255});
    chat->setOpacity(alpha(255.0f));
    chat->setPosition({bubbleSize * 0.5f, bubbleSize * 0.54f});
    m_bubbleRoot->addChild(chat);

    m_bubbleRoot->addChild(disc({126, 244, 255, alpha(210.0f)}, std::max(1.2f, bubbleSize * 0.045f), {bubbleSize * 0.32f, bubbleSize * 0.27f}));
    m_bubbleRoot->addChild(disc({255, 74, 105, alpha(210.0f)}, std::max(1.2f, bubbleSize * 0.045f), {bubbleSize * 0.68f, bubbleSize * 0.27f}));
}

void ComsPlusChatOverlay::buildPanel() {
    m_sendMenu = nullptr;
    m_touchPriorityRetries = 0;

    if (m_panelRoot) {
        m_panelRoot->removeFromParentAndCleanup(true);
    }

    auto size = panelSize();
    auto settings = readSettings();
    auto bgAlpha = static_cast<GLubyte>(std::clamp(settings.chatOpacity, 0.2f, 1.0f) * 228.0f);
    auto accent = panelAccent();
    auto headerHeight = isMobileLayout() ? 38.0f : 31.0f;
    auto inputHeight = isMobileLayout() ? 34.0f : 28.0f;
    m_lastPanelWidth = size.width;
    m_lastPanelHeight = size.height;
    m_lastChatOpacity = settings.chatOpacity;

    m_panelRoot = CCNode::create();
    m_panelRoot->setID("comsplus-chat-panel"_spr);
    m_panelRoot->setContentSize(size);
    this->addChild(m_panelRoot, 5);

    m_panelRoot->addChild(rect({accent.r, accent.g, accent.b, 28}, {size.width + 10.0f, size.height + 10.0f}, {-5.0f, -5.0f}));
    m_panelRoot->addChild(rect({6, 8, 15, bgAlpha}, size, {0.0f, 0.0f}));
    m_panelRoot->addChild(rect(isMainMenuContext() ? ccColor4B{10, 25, 47, 220} : ccColor4B{28, 8, 16, 220}, {size.width, headerHeight}, {0.0f, size.height - headerHeight}));
    m_panelRoot->addChild(rect({accent.r, accent.g, accent.b, 72}, {size.width - 12.0f, 1.0f}, {6.0f, size.height - headerHeight - 2.0f}));
    addBorder(m_panelRoot, size, {accent.r, accent.g, accent.b, 190}, 1.2f);
    auto innerBorder = CCNode::create();
    innerBorder->setPosition({5.0f, 5.0f});
    addBorder(innerBorder, {size.width - 10.0f, size.height - 10.0f}, {accent.r, accent.g, accent.b, 82}, 1.0f);
    m_panelRoot->addChild(innerBorder);

    auto title = CCLabelBMFont::create("ComsPlus", "bigFont.fnt");
    title->setAnchorPoint({0.0f, 0.5f});
    title->setScale(isMobileLayout() ? 0.44f : 0.37f);
    title->setColor({255, 230, 232});
    title->setPosition({12.0f, size.height - headerHeight * 0.5f});
    m_panelRoot->addChild(title);

    if (isMobileLayout()) {
        auto closeHint = CCLabelBMFont::create("x", "bigFont.fnt");
        closeHint->setAnchorPoint({1.0f, 0.5f});
        closeHint->setScale(0.24f);
        closeHint->setColor({255, 168, 176});
        closeHint->setPosition({size.width - 12.0f, size.height - headerHeight * 0.5f});
        m_panelRoot->addChild(closeHint);
    }

    m_status = CCLabelBMFont::create("ComsPlus", "chatFont.fnt");
    m_status->setID("comsplus-status"_spr);
    m_status->setAnchorPoint({1.0f, 0.5f});
    m_status->setScale(isMobileLayout() ? 0.38f : 0.33f);
    m_status->setColor({185, 222, 255});
    m_status->setPosition({size.width - (isMobileLayout() ? 28.0f : 12.0f), size.height - headerHeight * 0.5f});
    m_panelRoot->addChild(m_status);

    m_messageRoot = CCNode::create();
    m_messageRoot->setID("comsplus-messages"_spr);
    m_messageRoot->setContentSize({size.width - 18.0f, size.height - headerHeight - inputHeight - 17.0f});
    m_messageRoot->setPosition({9.0f, inputHeight + 11.0f});
    m_panelRoot->addChild(m_messageRoot);

    m_inputHitOrigin = CCPoint{10.0f, 7.0f};
    m_inputHitSize = CCSize{size.width - 102.0f, inputHeight};
    m_panelRoot->addChild(rect({2, 5, 13, 174}, m_inputHitSize, m_inputHitOrigin));
    auto inputBorder = CCNode::create();
    inputBorder->setPosition(m_inputHitOrigin);
    addBorder(inputBorder, m_inputHitSize, {accent.r, accent.g, accent.b, 92}, 1.0f);
    m_panelRoot->addChild(inputBorder);

    m_input = TextInput::create(size.width - 110.0f, "Message", "chatFont.fnt");
    m_input->setID("comsplus-input"_spr);
    m_input->setScale(isMobileLayout() ? 0.83f : 0.68f);
    m_input->setMaxCharCount(120);
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setPosition({(size.width - 100.0f) * 0.5f, 7.0f + inputHeight * 0.5f});
    m_panelRoot->addChild(m_input);

    auto sendSprite = ButtonSprite::create("Send", "chatFont.fnt", "GJ_button_06.png", 0.54f);
    auto sendButton = CCMenuItemSpriteExtra::create(sendSprite, this, menu_selector(ComsPlusChatOverlay::onSend));
    sendButton->setID("comsplus-send-button"_spr);
    auto menu = CCMenu::create();
    menu->setID("comsplus-send-menu"_spr);
    menu->setPosition({size.width - 42.0f, 7.0f + inputHeight * 0.5f});
    menu->addChild(sendButton);
    m_sendMenu = menu;
    m_panelRoot->addChild(menu);

    if (this->isRunning()) {
        this->scheduleOnce(schedule_selector(ComsPlusChatOverlay::applyTouchPrioritiesDelayed), 0.0f);
    }
}

void ComsPlusChatOverlay::applyTouchPriorities() {
    // Android crash logs showed CCTouchDispatcher::setPriority hitting a null handler
    // when this ran from buildPanel before CCMenu had entered and registered itself.
    if (!this->isRunning() || !m_sendMenu || !m_sendMenu->isRunning()) return;
    auto director = CCDirector::sharedDirector();
    auto dispatcher = director ? director->getTouchDispatcher() : nullptr;
    if (!dispatcher || !dispatcher->findHandler(m_sendMenu)) {
        if (m_touchPriorityRetries++ < 8) {
            this->scheduleOnce(schedule_selector(ComsPlusChatOverlay::applyTouchPrioritiesDelayed), 0.05f);
        }
        return;
    }
    m_touchPriorityRetries = 0;
    m_sendMenu->setHandlerPriority(kOverlayTouchPriority - 1);
}

void ComsPlusChatOverlay::applyTouchPrioritiesDelayed(float) {
    applyTouchPriorities();
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
    if (m_bubbleRoot) {
        auto settings = readSettings();
        auto hiddenInMenu = isMainMenuContext() && (settings.hideBubbleInMainMenu || !settings.mainMenuChatEnabled);
        m_bubbleRoot->setVisible(isMobileLayout() && !expanded && !hiddenInMenu);
    }
    if (m_panelRoot) m_panelRoot->setVisible(expanded);
    updateLayout();
}

void ComsPlusChatOverlay::refreshVisibility() {
    setExpanded(m_expanded);
}

bool ComsPlusChatOverlay::isExpanded() const {
    return m_expanded;
}

bool ComsPlusChatOverlay::submitFromKeyboard() {
    if (!m_expanded || !m_input) return false;
    onSend(nullptr);
    return true;
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
    if (scene) {
        moveToParent(scene, kOverlayZOrder);
    } else {
        this->setZOrder(kOverlayZOrder);
    }
}

void ComsPlusChatOverlay::moveToParent(CCNode* parent, int zOrder) {
    if (!parent) return;

    if (this->getParent() == parent) {
        this->setZOrder(zOrder);
        return;
    }

    m_reparenting = true;
    this->retain();
    if (this->getParent()) {
        this->removeFromParentAndCleanup(false);
    }
    parent->addChild(this, zOrder);
    this->release();
    m_reparenting = false;
    g_activeOverlay = this;
}

void ComsPlusChatOverlay::removeOverlay() {
    this->setTouchEnabled(false);
    if (this->getParent()) {
        this->removeFromParentAndCleanup(true);
    }
    if (g_activeOverlay == this) {
        g_activeOverlay = nullptr;
    }
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

    if (m_expanded && pointInPanelHeader(point)) {
        m_dragMode = DragMode::Panel;
        m_dragStart = m_panelPosition;
        return true;
    }

    if (m_expanded && pointInPanel(point)) {
        if (pointInInput(point) && m_input) {
            m_input->focus();
            m_dragMode = DragMode::None;
            return true;
        }
        if (auto accountId = accountIdAt(point)) {
            m_dragMode = DragMode::Message;
            m_pressedAccountId = *accountId;
            return true;
        }
        m_dragMode = DragMode::None;
        return true;
    }

    if (m_expanded && isMobileLayout()) {
        collapse();
        m_dragMode = DragMode::None;
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
    } else if (m_dragMode == DragMode::Panel && isMobileLayout() && !m_dragged) {
        collapse();
    } else if (m_dragMode == DragMode::Message && !m_dragged && m_pressedAccountId > 0) {
        openProfile(m_pressedAccountId);
    }
    m_pressedAccountId = 0;
    m_dragMode = DragMode::None;
}

void ComsPlusChatOverlay::tick(float dt) {
    m_elapsed += dt;
    m_rainbowClock += dt;
    if (m_elapsed < 0.25f) return;
    m_elapsed = 0.0f;

    if (isMainMenuContext()) {
        GlobalChatBridge::get().maintain();
    } else {
        GlobedBridge::get().maintain();
    }
    pruneExpiredBans();

    auto settings = readSettings();
    auto wantedPanelSize = panelSize();
    if (
        m_panelRoot &&
        (
            std::abs(wantedPanelSize.width - m_lastPanelWidth) > 0.5f ||
            std::abs(wantedPanelSize.height - m_lastPanelHeight) > 0.5f ||
            std::abs(settings.chatOpacity - m_lastChatOpacity) > 0.01f
        )
    ) {
        buildPanel();
        rebuild();
        setExpanded(m_expanded);
    }

    if (m_status) {
        auto status = isMainMenuContext() ? GlobalChatBridge::get().statusText() : GlobedBridge::get().statusText();
        m_status->setString(status.c_str());
    }

    updateLayout();

    if (isMobileLayout() && m_bubbleRoot) {
        auto bubbleSize = std::clamp(settings.bubbleSize, 26.0f, 72.0f);
        auto bubbleOpacity = std::clamp(settings.bubbleOpacity, 0.25f, 1.0f);
        if (std::abs(bubbleSize - m_lastBubbleSize) > 0.5f || std::abs(bubbleOpacity - m_lastBubbleOpacity) > 0.01f) {
            buildBubble();
            updateLayout();
        }
    }

    if (!isMainMenuContext()) {
        announceJoinIfNeeded();
    }

    auto received = isMainMenuContext() ? GlobalChatBridge::get().pollReceived() : GlobedBridge::get().pollReceived();
    auto receivedAny = false;
    for (auto& message : received) {
        if (hasMessageId(message.messageId)) continue;
        if (!shouldDisplayMessage(message)) continue;
        if (message.kind == ChatMessageKind::Moderation) {
            if (!applyModeration(message)) continue;
            receivedAny = true;
            appendMessage(std::move(message), false);
            continue;
        }
        if (isMessageBanned(message)) continue;
        receivedAny = true;
        appendMessage(std::move(message), false);
    }
    if (!receivedAny && hasRainbowMessages()) {
        rebuild();
    }
}

void ComsPlusChatOverlay::onSend(CCObject*) {
    if (!m_input) return;

    auto text = sanitizeMessage(std::string(m_input->getString()));
    if (text.empty()) return;

    if (!text.empty() && text.front() == '/') {
        if (handleCommand(text)) {
            m_input->setString("", false);
        }
        return;
    }

    auto ban = localBan();
    if (ban.has_value()) {
        if (m_status) m_status->setString(("Chat banned: " + ban->reason).c_str());
        return;
    }

    auto current = nowMs();
    if (m_rateLimiter && !m_rateLimiter->canSend(current)) {
        if (m_status) m_status->setString("Slow down");
        return;
    }

    auto message = makeLocalMessage(text);
    auto result = isMainMenuContext() ? GlobalChatBridge::get().sendChat(message) : GlobedBridge::get().sendChat(message);
    if (result == ChatSendResult::Failed) {
        if (m_status) {
            auto status = isMainMenuContext() ? GlobalChatBridge::get().statusText() : GlobedBridge::get().statusText();
            m_status->setString(status.c_str());
        }
        return;
    }

    if (m_rateLimiter) m_rateLimiter->markSent(current);
    m_input->setString("", false);
    if (result == ChatSendResult::Queued && m_status) {
        m_status->setString(isMainMenuContext() ? "Queued for main chat" : "Queued for Globed");
    }
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

void ComsPlusChatOverlay::announceJoinIfNeeded() {
    if (!GlobedBridge::get().isConnected()) return;

    auto levelName = currentLevelName();
    auto levelKey = currentLevelKey();
    if (m_joinedLevelKey == levelKey) return;

    auto message = makeSystemMessage("joined the chat from " + levelName);
    auto result = GlobedBridge::get().sendChat(message);
    if (result == ChatSendResult::Failed) return;

    m_joinedLevelKey = levelKey;
    appendMessage(std::move(message), true);
}

ChatMessage ComsPlusChatOverlay::makeSystemMessage(std::string text) const {
    auto message = makeLocalMessage(std::move(text));
    message.kind = ChatMessageKind::System;
    message.iconData = "";
    return message;
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
    message.kind = ChatMessageKind::User;
    message.levelId = currentLevelId();
    message.levelName = currentLevelName();
    message.colorMode = settings.ownMessageColorMode;
    message.primaryColor = colorToWire(settings.ownMessagePrimaryColor);
    message.secondaryColor = colorToWire(settings.ownMessageSecondaryColor);
    if (isLocalDevUser()) {
        message.authorRole = ChatAuthorRole::Dev;
    }
    return message;
}

ChatMessage ComsPlusChatOverlay::makeModerationMessage(
    ChatModerationAction action,
    std::string targetName,
    std::int64_t targetAccountId,
    std::string reason,
    std::int64_t expiresAt
) const {
    auto message = makeLocalMessage("");
    auto cleanTarget = sanitizeName(targetName);
    auto cleanReason = sanitizeMessage(reason);
    if (cleanReason.empty()) cleanReason = "No reason";

    message.kind = ChatMessageKind::Moderation;
    message.iconData = "";
    message.authorRole = ChatAuthorRole::Dev;
    message.moderationAction = action;
    message.targetName = cleanTarget;
    message.targetAccountId = targetAccountId;
    message.expiresAt = expiresAt;

    if (action == ChatModerationAction::TempBan) {
        auto duration = std::max<std::int64_t>(1000, expiresAt - nowMs());
        message.text = sanitizeMessage("tempbanned " + cleanTarget + " for " + durationText(duration) + ". Reason: " + cleanReason);
    } else {
        message.text = sanitizeMessage("banned " + cleanTarget + ". Reason: " + cleanReason);
    }

    return message;
}

bool ComsPlusChatOverlay::handleCommand(std::string const& text) {
    auto parts = parseCommandArgs(text);
    if (parts.empty()) return false;

    auto command = lowercaseAscii(parts.front());
    auto isBan = command == "/ban";
    auto isTempBan = command == "/tempban";
    if (!isBan && !isTempBan) {
        if (m_status) m_status->setString("Unknown command");
        return true;
    }

    if (!isLocalDevUser()) {
        if (m_status) m_status->setString("Dev command only");
        return true;
    }

    if (parts.size() < 3) {
        if (m_status) m_status->setString(isTempBan ? "/tempban \"User\" [1h] \"Reason\"" : "/ban \"User\" \"Reason\"");
        return false;
    }

    auto targetName = sanitizeName(parts[1]);
    if (targetName.empty()) {
        if (m_status) m_status->setString("Missing target");
        return false;
    }

    auto reasonStart = std::size_t{2};
    auto durationMs = kDefaultTempBanMs;
    if (isTempBan && parts.size() >= 4) {
        if (auto parsed = parseDurationMs(parts[2])) {
            durationMs = *parsed;
            reasonStart = 3;
        }
    }

    auto reason = sanitizeMessage(joinArgs(parts, reasonStart));
    if (reason.empty()) reason = "No reason";

    auto targetAccountId = accountIdForName(targetName).value_or(0);
    auto expiresAt = isTempBan ? nowMs() + durationMs : 0;
    auto message = makeModerationMessage(
        isTempBan ? ChatModerationAction::TempBan : ChatModerationAction::Ban,
        targetName,
        targetAccountId,
        reason,
        expiresAt
    );

    auto result = isMainMenuContext() ? GlobalChatBridge::get().sendChat(message) : GlobedBridge::get().sendChat(message);
    if (result == ChatSendResult::Failed) {
        if (m_status) {
            auto status = isMainMenuContext() ? GlobalChatBridge::get().statusText() : GlobedBridge::get().statusText();
            m_status->setString(status.c_str());
        }
        return false;
    }

    applyModeration(message);
    appendMessage(std::move(message), true);
    if (m_status) {
        m_status->setString(result == ChatSendResult::Queued ? "Mod command queued" : "Mod command sent");
    }
    return true;
}

bool ComsPlusChatOverlay::applyModeration(ChatMessage const& message) {
    if (message.kind != ChatMessageKind::Moderation || message.authorRole != ChatAuthorRole::Dev) {
        return false;
    }
    if (
        message.moderationAction != ChatModerationAction::Ban &&
        message.moderationAction != ChatModerationAction::TempBan
    ) {
        return false;
    }

    auto targetName = sanitizeName(message.targetName);
    if (targetName.empty()) return false;

    ChatBan ban;
    ban.targetName = targetName;
    ban.targetAccountId = message.targetAccountId;
    ban.reason = moderationReason(message.text);
    ban.expiresAt = message.moderationAction == ChatModerationAction::TempBan ? message.expiresAt : 0;
    ban.moderatorName = sanitizeName(message.displayName);

    if (ban.expiresAt > 0 && ban.expiresAt <= nowMs()) {
        return false;
    }

    auto sameTarget = [&](ChatBan const& existing) {
        if (ban.targetAccountId != 0 && existing.targetAccountId == ban.targetAccountId) return true;
        return namesEqual(existing.targetName, ban.targetName);
    };
    m_bans.erase(std::remove_if(m_bans.begin(), m_bans.end(), sameTarget), m_bans.end());
    m_bans.push_back(std::move(ban));

    m_messages.erase(
        std::remove_if(m_messages.begin(), m_messages.end(), [&](RenderedMessage const& rendered) {
            return rendered.message.kind == ChatMessageKind::User && isMessageBanned(rendered.message);
        }),
        m_messages.end()
    );
    return true;
}

void ComsPlusChatOverlay::pruneExpiredBans() {
    auto now = nowMs();
    m_bans.erase(
        std::remove_if(m_bans.begin(), m_bans.end(), [now](ChatBan const& ban) {
            return ban.expiresAt > 0 && ban.expiresAt <= now;
        }),
        m_bans.end()
    );
}

bool ComsPlusChatOverlay::isMessageBanned(ChatMessage const& message) const {
    if (message.kind != ChatMessageKind::User) return false;
    for (auto const& ban : m_bans) {
        if (ban.expiresAt > 0 && ban.expiresAt <= nowMs()) continue;
        if (ban.targetAccountId != 0 && message.accountId == ban.targetAccountId) return true;
        if (namesEqual(ban.targetName, message.displayName)) return true;
    }
    return false;
}

std::optional<ComsPlusChatOverlay::ChatBan> ComsPlusChatOverlay::localBan() const {
    auto accountId = localAccountId();
    auto settings = readSettings();
    DisplayNameSettings displaySettings{
        settings.privacyEnabled,
        settings.fakeName,
        settings.chatNameMode
    };
    auto displayName = selectDisplayName(localRealName(), displaySettings);
    auto realNames = localRealNameCandidates();

    for (auto const& ban : m_bans) {
        if (ban.expiresAt > 0 && ban.expiresAt <= nowMs()) continue;
        if (ban.targetAccountId != 0 && accountId != 0 && ban.targetAccountId == accountId) return ban;
        if (namesEqual(ban.targetName, displayName)) return ban;
        if (std::any_of(realNames.begin(), realNames.end(), [&](std::string const& realName) {
            return namesEqual(ban.targetName, realName);
        })) {
            return ban;
        }
    }
    return std::nullopt;
}

std::optional<std::int64_t> ComsPlusChatOverlay::accountIdForName(std::string const& name) const {
    for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it) {
        if (it->message.accountId != 0 && namesEqual(it->message.displayName, name)) {
            return it->message.accountId;
        }
    }
    return std::nullopt;
}

bool ComsPlusChatOverlay::shouldDisplayMessage(ChatMessage const& message) const {
    auto levelId = currentLevelId();
    if (message.levelId != 0 && levelId != 0) {
        return message.levelId == levelId;
    }

    if (!message.levelName.empty()) {
        return sanitizeName(message.levelName) == currentLevelName();
    }

    return true;
}

bool ComsPlusChatOverlay::hasRainbowMessages() const {
    return std::any_of(m_messages.begin(), m_messages.end(), [](RenderedMessage const& rendered) {
        return rendered.message.colorMode == ChatColorMode::Rainbow;
    });
}

bool ComsPlusChatOverlay::hasMessageId(std::string const& messageId) const {
    if (messageId.empty()) return false;
    return std::any_of(m_messages.begin(), m_messages.end(), [&](RenderedMessage const& rendered) {
        return rendered.message.messageId == messageId;
    });
}

CCNode* ComsPlusChatOverlay::createIconNode(std::string const& iconData) const {
    auto cube = parseIconPart(iconData, "cube", 1);
    auto color1 = parseIconPart(iconData, "c1", 0);
    auto color2 = parseIconPart(iconData, "c2", 3);

    auto icon = SimplePlayer::create(cube);
    if (icon) {
        icon->setColor(colorFor(color1));
        icon->setSecondColor(colorFor(color2));
        icon->setScale(isMobileLayout() ? 0.56f : 0.42f);
        return icon;
    }

    auto fallback = CCLabelBMFont::create("[]", "chatFont.fnt");
    fallback->setScale(isMobileLayout() ? 0.82f : 0.62f);
    return fallback;
}

void ComsPlusChatOverlay::rebuild() {
    if (!m_messageRoot) return;
    m_messageRoot->removeAllChildrenWithCleanup(true);
    m_messageHits.clear();

    auto rootSize = m_messageRoot->getContentSize();
    float y = 3.0f;
    auto phase = m_rainbowClock * 3.2f;
    auto index = 0;

    for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it, ++index) {
        auto const& rendered = *it;
        if (!shouldDisplayMessage(rendered.message)) continue;
        if (rendered.message.kind == ChatMessageKind::System || rendered.message.kind == ChatMessageKind::Moderation) {
            auto rowHeight = isMobileLayout() ? 21.0f : 17.0f;
            if (y + rowHeight > rootSize.height) break;

            auto isModeration = rendered.message.kind == ChatMessageKind::Moderation;
            auto line = isModeration ?
                "Dev " + rendered.message.displayName + " " + rendered.message.text :
                rendered.message.displayName + " " + rendered.message.text;
            auto row = rect(
                isModeration ? ccColor4B{18, 42, 58, 56} : ccColor4B{42, 14, 22, 46},
                {rootSize.width, rowHeight},
                {0.0f, y}
            );
            m_messageRoot->addChild(row);

            auto label = CCLabelBMFont::create(line.c_str(), "chatFont.fnt");
            label->setAnchorPoint({0.0f, 0.5f});
            auto systemScale = isMobileLayout() ? 0.43f : 0.34f;
            label->setScale(systemScale);
            label->setColor(isModeration ? ccColor3B{155, 224, 255} : ccColor3B{198, 153, 163});
            label->setPosition({6.0f, y + rowHeight * 0.5f});
            label->limitLabelWidth(rootSize.width - 12.0f, systemScale, 0.06f);
            m_messageRoot->addChild(label);

            y += rowHeight + 2.0f;
            continue;
        }

        auto rowHeight = isMobileLayout() ? 56.0f : 46.0f;
        if (y + rowHeight > rootSize.height) break;

        auto rowPhase = phase + static_cast<float>(index) * 0.45f;
        auto accent = messageNameColor(rendered.message, rendered.local, rowPhase);
        auto row = rect(rowBackground(rendered.message, rendered.local, rowPhase), {rootSize.width, rowHeight}, {0.0f, y});
        m_messageRoot->addChild(row);
        m_messageRoot->addChild(rect({accent.r, accent.g, accent.b, 160}, {2.0f, rowHeight - 6.0f}, {0.0f, y + 3.0f}));

        auto icon = createIconNode(rendered.message.iconData);
        icon->setAnchorPoint({0.5f, 0.5f});
        auto iconCenterX = isMobileLayout() ? 30.0f : 22.0f;
        auto iconCenterY = y + rowHeight - (isMobileLayout() ? 18.0f : 15.5f);
        icon->setPosition({iconCenterX, iconCenterY});
        m_messageRoot->addChild(icon);

        auto name = CCLabelBMFont::create(rendered.message.displayName.c_str(), "chatFont.fnt");
        name->setAnchorPoint({0.0f, 0.5f});
        auto nameScale = isMobileLayout() ? 0.57f : 0.46f;
        auto textScale = isMobileLayout() ? 0.48f : 0.39f;
        auto textX = isMobileLayout() ? 66.0f : 49.0f;
        name->setScale(nameScale);
        name->setColor(accent);
        name->setPosition({textX, y + rowHeight - (isMobileLayout() ? 16.0f : 13.5f)});
        name->limitLabelWidth(rootSize.width - textX - 62.0f, nameScale, 0.1f);
        m_messageRoot->addChild(name);

        if (hasDevBadge(rendered.message)) {
            auto nameWidth = std::min(name->getScaledContentWidth(), rootSize.width - textX - 76.0f);
            auto badgeX = textX + nameWidth + 8.0f;
            auto badgeY = y + rowHeight - (isMobileLayout() ? 15.6f : 13.2f);

            auto badge = CCLabelBMFont::create("Dev", "chatFont.fnt");
            badge->setAnchorPoint({0.0f, 0.5f});
            badge->setScale(isMobileLayout() ? 0.34f : 0.27f);
            badge->setColor({135, 226, 255});
            badge->setPosition({badgeX, badgeY});
            badge->limitLabelWidth(40.0f, isMobileLayout() ? 0.34f : 0.27f, 0.1f);
            m_messageRoot->addChild(badge);
        }

        auto text = CCLabelBMFont::create(rendered.message.text.c_str(), "chatFont.fnt");
        text->setAnchorPoint({0.0f, 0.5f});
        text->setScale(textScale);
        text->setColor(messageBodyColor(rendered.message, rendered.local, rowPhase));
        text->setPosition({textX, y + (isMobileLayout() ? 15.0f : 12.0f)});
        text->limitLabelWidth(rootSize.width - textX - 12.0f, textScale, 0.075f);
        m_messageRoot->addChild(text);

        if (rendered.message.accountId > 0) {
            m_messageHits.push_back({CCRectMake(0.0f, y, rootSize.width, rowHeight), rendered.message.accountId});
        }

        y += rowHeight + 3.0f;
    }
}

std::optional<std::int64_t> ComsPlusChatOverlay::accountIdAt(CCPoint const& point) const {
    if (!m_messageRoot) return std::nullopt;
    auto local = m_messageRoot->convertToNodeSpace(point);
    for (auto const& hit : m_messageHits) {
        if (
            local.x >= hit.rect.origin.x &&
            local.x <= hit.rect.origin.x + hit.rect.size.width &&
            local.y >= hit.rect.origin.y &&
            local.y <= hit.rect.origin.y + hit.rect.size.height
        ) {
            return hit.accountId;
        }
    }
    return std::nullopt;
}

void ComsPlusChatOverlay::openProfile(std::int64_t accountId) {
    if (accountId <= 0) return;
    if (auto page = ProfilePage::create(static_cast<int>(accountId), accountId == localAccountId())) {
        page->show();
    }
}

bool ComsPlusChatOverlay::pointInBubble(CCPoint const& point) const {
    return m_bubbleRoot && pointInRect(point, m_bubblePosition, m_bubbleRoot->getContentSize());
}

bool ComsPlusChatOverlay::pointInInput(CCPoint const& point) const {
    if (!m_input || !m_panelRoot) return false;
    if (pointInRect(point, add(m_panelPosition, m_inputHitOrigin), m_inputHitSize)) {
        return true;
    }
    auto local = m_input->convertToNodeSpace(point);
    auto size = m_input->getContentSize();
    auto anchor = m_input->getAnchorPoint();
    return local.x >= -size.width * anchor.x &&
        local.x <= size.width * (1.0f - anchor.x) &&
        local.y >= -size.height * anchor.y &&
        local.y <= size.height * (1.0f - anchor.y);
}

bool ComsPlusChatOverlay::pointInPanel(CCPoint const& point) const {
    return m_panelRoot && pointInRect(point, m_panelPosition, m_panelRoot->getContentSize());
}

bool ComsPlusChatOverlay::pointInPanelHeader(CCPoint const& point) const {
    if (!m_panelRoot) return false;
    auto size = m_panelRoot->getContentSize();
    auto headerHeight = isMobileLayout() ? 36.0f : 30.0f;
    return pointInRect(
        point,
        {m_panelPosition.x, m_panelPosition.y + size.height - headerHeight},
        {size.width, headerHeight}
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
