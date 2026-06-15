#pragma once

#include "ChatCore.hpp"
#include "ComsPlusSettings.hpp"
#include "GlobedBridge.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <deque>
#include <memory>
#include <string>

namespace comsplus {

class ComsPlusChatOverlay : public cocos2d::CCLayer {
public:
    static ComsPlusChatOverlay* create();

    bool init() override;
    void onExit() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void tick(float dt);
    void onSend(cocos2d::CCObject*);
    void collapse();
    void togglePanel();
    void raiseToScene();

private:
    enum class DragMode {
        None,
        Bubble,
        Panel
    };

    struct RenderedMessage {
        ChatMessage message;
        bool local = false;
    };

    void buildBubble();
    void buildPanel();
    void updateLayout();
    void rebuild();
    void setExpanded(bool expanded);
    void openPanel();
    void appendMessage(ChatMessage message, bool local);
    void announceJoinIfNeeded();
    ChatMessage makeSystemMessage(std::string text) const;
    ChatMessage makeLocalMessage(std::string text) const;
    cocos2d::CCNode* createIconNode(std::string const& iconData) const;
    bool pointInBubble(cocos2d::CCPoint const& point) const;
    bool pointInPanelHeader(cocos2d::CCPoint const& point) const;
    cocos2d::CCPoint clampedBubblePosition(cocos2d::CCPoint position) const;
    cocos2d::CCPoint clampedPanelPosition(cocos2d::CCPoint position) const;

    cocos2d::CCNode* m_bubbleRoot = nullptr;
    cocos2d::CCNode* m_panelRoot = nullptr;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCNode* m_messageRoot = nullptr;
    std::deque<RenderedMessage> m_messages;
    std::unique_ptr<RateLimiter> m_rateLimiter;
    cocos2d::CCPoint m_bubblePosition = {0.0f, 0.0f};
    cocos2d::CCPoint m_panelPosition = {0.0f, 0.0f};
    cocos2d::CCPoint m_touchStart = {0.0f, 0.0f};
    cocos2d::CCPoint m_dragStart = {0.0f, 0.0f};
    DragMode m_dragMode = DragMode::None;
    bool m_expanded = false;
    bool m_dragged = false;
    float m_lastBubbleSize = 0.0f;
    float m_lastBubbleOpacity = 0.0f;
    float m_lastPanelWidth = 0.0f;
    float m_lastPanelHeight = 0.0f;
    float m_lastChatOpacity = 0.0f;
    float m_elapsed = 0.0f;
    std::string m_joinedLevelKey;
};

ComsPlusChatOverlay* activeChatOverlay();
void collapseActiveChatOverlay();
void toggleActiveChatOverlay();
void raiseActiveChatOverlay();

} // namespace comsplus
