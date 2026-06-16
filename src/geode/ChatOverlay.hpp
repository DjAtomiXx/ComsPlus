#pragma once

#include "ChatCore.hpp"
#include "ComsPlusSettings.hpp"
#include "GlobedBridge.hpp"
#include "GlobalChatBridge.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace comsplus {

class ComsPlusChatOverlay : public cocos2d::CCLayer {
public:
    static ComsPlusChatOverlay* create();

    bool init() override;
    void onEnter() override;
    void onExit() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void tick(float dt);
    void onSend(cocos2d::CCObject*);
    void collapse();
    void togglePanel();
    void raiseToScene();
    void moveToParent(cocos2d::CCNode* parent, int zOrder);
    void removeOverlay();
    void refreshVisibility();
    bool isExpanded() const;
    bool submitFromKeyboard();

private:
    enum class DragMode {
        None,
        Bubble,
        Panel,
        Message
    };

    struct RenderedMessage {
        ChatMessage message;
        bool local = false;
    };

    struct ChatBan {
        std::string targetName;
        std::int64_t targetAccountId = 0;
        std::string reason;
        std::int64_t expiresAt = 0;
        std::string moderatorName;
    };

    struct MessageHit {
        cocos2d::CCRect rect;
        std::int64_t accountId = 0;
    };

    void buildBubble();
    void buildPanel();
    void applyTouchPriorities();
    void applyTouchPrioritiesDelayed(float);
    void updateLayout();
    void rebuild();
    void setExpanded(bool expanded);
    void openPanel();
    void appendMessage(ChatMessage message, bool local);
    void announceJoinIfNeeded();
    ChatMessage makeSystemMessage(std::string text) const;
    ChatMessage makeLocalMessage(std::string text) const;
    ChatMessage makeModerationMessage(
        ChatModerationAction action,
        std::string targetName,
        std::int64_t targetAccountId,
        std::string reason,
        std::int64_t expiresAt
    ) const;
    cocos2d::CCNode* createIconNode(std::string const& iconData) const;
    bool handleCommand(std::string const& text);
    bool applyModeration(ChatMessage const& message);
    void pruneExpiredBans();
    bool isMessageBanned(ChatMessage const& message) const;
    std::optional<ChatBan> localBan() const;
    std::optional<std::int64_t> accountIdForName(std::string const& name) const;
    bool shouldDisplayMessage(ChatMessage const& message) const;
    bool hasRainbowMessages() const;
    bool hasMessageId(std::string const& messageId) const;
    bool pointInBubble(cocos2d::CCPoint const& point) const;
    bool pointInInput(cocos2d::CCPoint const& point) const;
    bool pointInPanel(cocos2d::CCPoint const& point) const;
    bool pointInPanelHeader(cocos2d::CCPoint const& point) const;
    std::optional<std::int64_t> accountIdAt(cocos2d::CCPoint const& point) const;
    void openProfile(std::int64_t accountId);
    cocos2d::CCPoint clampedBubblePosition(cocos2d::CCPoint position) const;
    cocos2d::CCPoint clampedPanelPosition(cocos2d::CCPoint position) const;

    cocos2d::CCNode* m_bubbleRoot = nullptr;
    cocos2d::CCNode* m_panelRoot = nullptr;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCMenu* m_sendMenu = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCNode* m_messageRoot = nullptr;
    std::deque<RenderedMessage> m_messages;
    std::vector<MessageHit> m_messageHits;
    std::vector<ChatBan> m_bans;
    std::unique_ptr<RateLimiter> m_rateLimiter;
    cocos2d::CCPoint m_bubblePosition = {0.0f, 0.0f};
    cocos2d::CCPoint m_panelPosition = {0.0f, 0.0f};
    cocos2d::CCPoint m_inputHitOrigin = {0.0f, 0.0f};
    cocos2d::CCSize m_inputHitSize = {0.0f, 0.0f};
    cocos2d::CCPoint m_touchStart = {0.0f, 0.0f};
    cocos2d::CCPoint m_dragStart = {0.0f, 0.0f};
    std::int64_t m_pressedAccountId = 0;
    DragMode m_dragMode = DragMode::None;
    bool m_expanded = false;
    bool m_dragged = false;
    bool m_reparenting = false;
    float m_lastBubbleSize = 0.0f;
    float m_lastBubbleOpacity = 0.0f;
    float m_lastPanelWidth = 0.0f;
    float m_lastPanelHeight = 0.0f;
    float m_lastChatOpacity = 0.0f;
    float m_elapsed = 0.0f;
    float m_rainbowClock = 0.0f;
    int m_touchPriorityRetries = 0;
    std::string m_joinedLevelKey;
};

ComsPlusChatOverlay* activeChatOverlay();
void collapseActiveChatOverlay();
void toggleActiveChatOverlay();
void raiseActiveChatOverlay();

} // namespace comsplus
