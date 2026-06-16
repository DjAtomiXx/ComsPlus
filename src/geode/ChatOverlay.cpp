#include "ChatOverlay.hpp"

#include <Geode/Bindings.hpp>
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
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

std::string currentLevelName() {
    if (auto gm = GameManager::get()) {
        if (gm->m_playLayer && gm->m_playLayer->m_level) {
            auto name = sanitizeName(std::string(gm->m_playLayer->m_level->m_levelName));
            if (!name.empty()) {
                return name;
            }
        }
    }
    return "this level";
}

std::int64_t currentLevelId() {
    if (auto gm = GameManager::get()) {
        if (gm->m_playLayer && gm->m_playLayer->m_level) {
            return static_cast<std::int64_t>(gm->m_playLayer->m_level->m_levelID);
        }
    }
    return 0;
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
    auto base = local ? ccColor3B{11, 36, 34} : ccColor3B{29, 15, 23};
    auto mixed = blendColor(base, accent, local ? 0.18f : 0.12f);
    return {mixed.r, mixed.g, mixed.b, local ? static_cast<GLubyte>(94) : static_cast<GLubyte>(78)};
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
    m_sendMenu = nullptr;
    m_touchPriorityRetries = 0;

    if (m_panelRoot) {
        m_panelRoot->removeFromParentAndCleanup(true);
    }

    auto size = panelSize();
    auto settings = readSettings();
    auto bgAlpha = static_cast<GLubyte>(std::clamp(settings.chatOpacity, 0.2f, 1.0f) * 228.0f);
    m_lastPanelWidth = size.width;
    m_lastPanelHeight = size.height;
    m_lastChatOpacity = settings.chatOpacity;

    m_panelRoot = CCNode::create();
    m_panelRoot->setID("comsplus-chat-panel"_spr);
    m_panelRoot->setContentSize(size);
    this->addChild(m_panelRoot, 5);

    m_panelRoot->addChild(rect({255, 24, 50, 26}, {size.width + 10.0f, size.height + 10.0f}, {-5.0f, -5.0f}));
    m_panelRoot->addChild(rect({7, 7, 12, bgAlpha}, size, {0.0f, 0.0f}));
    m_panelRoot->addChild(rect({24, 8, 14, 205}, {size.width, 28.0f}, {0.0f, size.height - 28.0f}));
    m_panelRoot->addChild(rect({255, 46, 75, 58}, {size.width - 12.0f, 1.0f}, {6.0f, size.height - 30.0f}));
    addBorder(m_panelRoot, size, {255, 48, 78, 188}, 1.2f);
    auto innerBorder = CCNode::create();
    innerBorder->setPosition({5.0f, 5.0f});
    addBorder(innerBorder, {size.width - 10.0f, size.height - 10.0f}, {255, 68, 96, 78}, 1.0f);
    m_panelRoot->addChild(innerBorder);

    auto title = CCLabelBMFont::create("ComsPlus", "bigFont.fnt");
    title->setAnchorPoint({0.0f, 0.5f});
    title->setScale(0.34f);
    title->setColor({255, 230, 232});
    title->setPosition({12.0f, size.height - 14.0f});
    m_panelRoot->addChild(title);

    m_status = CCLabelBMFont::create("ComsPlus", "chatFont.fnt");
    m_status->setID("comsplus-status"_spr);
    m_status->setAnchorPoint({1.0f, 0.5f});
    m_status->setScale(0.31f);
    m_status->setColor({255, 150, 160});
    m_status->setPosition({size.width - 12.0f, size.height - 14.0f});
    m_panelRoot->addChild(m_status);

    m_messageRoot = CCNode::create();
    m_messageRoot->setID("comsplus-messages"_spr);
    m_messageRoot->setContentSize({size.width - 20.0f, size.height - 68.0f});
    m_messageRoot->setPosition({10.0f, 36.0f});
    m_panelRoot->addChild(m_messageRoot);

    m_panelRoot->addChild(rect({2, 3, 7, 146}, {size.width - 96.0f, 24.0f}, {10.0f, 8.0f}));
    auto inputBorder = CCNode::create();
    inputBorder->setPosition({10.0f, 8.0f});
    addBorder(inputBorder, {size.width - 96.0f, 24.0f}, {255, 58, 86, 84}, 1.0f);
    m_panelRoot->addChild(inputBorder);

    m_input = TextInput::create(size.width - 104.0f, "Message", "chatFont.fnt");
    m_input->setID("comsplus-input"_spr);
    m_input->setScale(0.62f);
    m_input->setMaxCharCount(120);
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setPosition({(size.width - 92.0f) * 0.5f, 20.0f});
    m_panelRoot->addChild(m_input);

    auto sendSprite = ButtonSprite::create("Send", "chatFont.fnt", "GJ_button_06.png", 0.54f);
    auto sendButton = CCMenuItemSpriteExtra::create(sendSprite, this, menu_selector(ComsPlusChatOverlay::onSend));
    sendButton->setID("comsplus-send-button"_spr);
    auto menu = CCMenu::create();
    menu->setID("comsplus-send-menu"_spr);
    menu->setPosition({size.width - 38.0f, 20.0f});
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

    if (m_expanded && !isMobileLayout() && pointInPanelHeader(point)) {
        m_dragMode = DragMode::Panel;
        m_dragStart = m_panelPosition;
        return true;
    }

    if (m_expanded && pointInPanel(point)) {
        if (pointInInput(point) && m_input) {
            m_input->focus();
        }
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
    }
    m_dragMode = DragMode::None;
}

void ComsPlusChatOverlay::tick(float dt) {
    m_elapsed += dt;
    m_rainbowClock += dt;
    if (m_elapsed < 0.25f) return;
    m_elapsed = 0.0f;

    GlobedBridge::get().maintain();
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
        m_status->setString(GlobedBridge::get().statusText().c_str());
    }

    updateLayout();

    if (isMobileLayout() && m_bubbleRoot) {
        auto bubbleSize = std::clamp(settings.bubbleSize, 34.0f, 72.0f);
        auto bubbleOpacity = std::clamp(settings.bubbleOpacity, 0.25f, 1.0f);
        if (std::abs(bubbleSize - m_lastBubbleSize) > 0.5f || std::abs(bubbleOpacity - m_lastBubbleOpacity) > 0.01f) {
            buildBubble();
            updateLayout();
        }
    }

    announceJoinIfNeeded();

    auto received = GlobedBridge::get().pollReceived();
    auto receivedAny = false;
    for (auto& message : received) {
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
    auto result = GlobedBridge::get().sendChat(message);
    if (result == ChatSendResult::Failed) {
        if (m_status) m_status->setString(GlobedBridge::get().statusText().c_str());
        return;
    }

    if (m_rateLimiter) m_rateLimiter->markSent(current);
    m_input->setString("", false);
    if (result == ChatSendResult::Queued && m_status) {
        m_status->setString("Queued for Globed");
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

    auto result = GlobedBridge::get().sendChat(message);
    if (result == ChatSendResult::Failed) {
        if (m_status) m_status->setString(GlobedBridge::get().statusText().c_str());
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
    float y = 3.0f;
    auto phase = m_rainbowClock * 3.2f;
    auto index = 0;

    for (auto it = m_messages.rbegin(); it != m_messages.rend(); ++it, ++index) {
        auto const& rendered = *it;
        if (rendered.message.kind == ChatMessageKind::System || rendered.message.kind == ChatMessageKind::Moderation) {
            constexpr float rowHeight = 10.0f;
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
            label->setScale(0.235f);
            label->setColor(isModeration ? ccColor3B{155, 224, 255} : ccColor3B{198, 153, 163});
            label->setPosition({6.0f, y + rowHeight * 0.5f});
            label->limitLabelWidth(rootSize.width - 12.0f, 0.235f, 0.06f);
            m_messageRoot->addChild(label);

            y += rowHeight + 2.0f;
            continue;
        }

        constexpr float rowHeight = 24.0f;
        if (y + rowHeight > rootSize.height) break;

        auto rowPhase = phase + static_cast<float>(index) * 0.45f;
        auto accent = messageNameColor(rendered.message, rendered.local, rowPhase);
        auto row = rect(rowBackground(rendered.message, rendered.local, rowPhase), {rootSize.width, rowHeight}, {0.0f, y});
        m_messageRoot->addChild(row);
        m_messageRoot->addChild(rect({accent.r, accent.g, accent.b, 160}, {2.0f, rowHeight - 6.0f}, {0.0f, y + 3.0f}));

        auto icon = createIconNode(rendered.message.iconData);
        icon->setAnchorPoint({0.5f, 0.5f});
        icon->setPosition({13.0f, y + 16.0f});
        m_messageRoot->addChild(icon);

        auto name = CCLabelBMFont::create(rendered.message.displayName.c_str(), "chatFont.fnt");
        name->setAnchorPoint({0.0f, 0.5f});
        name->setScale(0.27f);
        name->setColor(accent);
        name->setPosition({29.0f, y + 16.5f});
        name->limitLabelWidth(rootSize.width - 38.0f, 0.27f, 0.07f);
        m_messageRoot->addChild(name);

        if (hasDevBadge(rendered.message)) {
            auto nameWidth = std::min(name->getScaledContentWidth(), rootSize.width - 82.0f);
            auto badgeX = 32.0f + nameWidth;
            auto badgeY = y + 12.5f;
            m_messageRoot->addChild(rect({77, 190, 255, 68}, {21.0f, 8.0f}, {badgeX, badgeY}));
            auto badgeBorder = CCNode::create();
            badgeBorder->setPosition({badgeX, badgeY});
            addBorder(badgeBorder, {21.0f, 8.0f}, {130, 225, 255, 128}, 0.7f);
            m_messageRoot->addChild(badgeBorder);

            auto badge = CCLabelBMFont::create("Dev", "chatFont.fnt");
            badge->setAnchorPoint({0.5f, 0.5f});
            badge->setScale(0.17f);
            badge->setColor({180, 238, 255});
            badge->setPosition({badgeX + 10.5f, badgeY + 4.0f});
            m_messageRoot->addChild(badge);
        }

        auto text = CCLabelBMFont::create(rendered.message.text.c_str(), "chatFont.fnt");
        text->setAnchorPoint({0.0f, 0.5f});
        text->setScale(0.245f);
        text->setColor(messageBodyColor(rendered.message, rendered.local, rowPhase));
        text->setPosition({29.0f, y + 6.0f});
        text->limitLabelWidth(rootSize.width - 38.0f, 0.245f, 0.055f);
        m_messageRoot->addChild(text);

        y += rowHeight + 3.0f;
    }
}

bool ComsPlusChatOverlay::pointInBubble(CCPoint const& point) const {
    return m_bubbleRoot && pointInRect(point, m_bubblePosition, m_bubbleRoot->getContentSize());
}

bool ComsPlusChatOverlay::pointInInput(CCPoint const& point) const {
    if (!m_input || !m_panelRoot) return false;
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
