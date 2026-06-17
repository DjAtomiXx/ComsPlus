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
        Message,
        List
    };

    enum class ViewMode {
        Chat,
        Players,
        Activity,
        Commands,
        Reports,
        ReportHistory,
        Blocks
    };

    struct TabHit {
        cocos2d::CCRect rect;
        ViewMode mode = ViewMode::Chat;
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

    struct ChatMute {
        std::string targetName;
        std::int64_t targetAccountId = 0;
        std::string reason;
        std::string moderatorName;
    };

    struct ChatBlock {
        std::string targetName;
        std::int64_t targetAccountId = 0;
    };

    struct ChatReport {
        ChatMessage message;
    };

    struct MessageHit {
        cocos2d::CCRect rect;
        std::int64_t accountId = 0;
    };

    enum class ActionHitType {
        None,
        Unblock,
        Report
    };

    struct ActionHit {
        cocos2d::CCRect rect;
        ActionHitType type = ActionHitType::None;
        std::string targetName;
        std::int64_t targetAccountId = 0;
    };

    void buildBubble();
    void buildPanel();
    void applyTouchPriorities();
    void applyTouchPrioritiesDelayed(float);
    void updateLayout();
    void rebuild();
    void renderMessages();
    void renderPresence();
    void renderActivity();
    void renderCommands();
    void renderReports();
    void renderReportHistory();
    void renderBlocks();
    void setExpanded(bool expanded);
    void openPanel();
    void appendMessage(ChatMessage message, bool local);
    void restorePersistentMainChat();
    void persistMainChatMessage(ChatMessage const& message, bool local, bool visible);
    void clearPersistentMainChat();
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
    bool isMessageMuted(ChatMessage const& message) const;
    bool isMessageBlocked(ChatMessage const& message) const;
    std::optional<ChatBan> localBan() const;
    std::optional<ChatMute> localMute() const;
    std::optional<std::int64_t> accountIdForName(std::string const& name) const;
    std::optional<ChatMessage> latestReportTarget() const;
    void addLocalBlock(std::string targetName, std::int64_t targetAccountId);
    void removeLocalBlock(std::string targetName, std::int64_t targetAccountId);
    void rememberReport(ChatMessage message);
    bool shouldDisplayMessage(ChatMessage const& message) const;
    bool hasRainbowMessages() const;
    bool hasMessageId(std::string const& messageId) const;
    bool canUseMetaTabs() const;
    std::vector<ChatPresence> presenceRows() const;
    std::vector<ChatActivity> activityRows() const;
    bool pointInTab(cocos2d::CCPoint const& point, ViewMode& mode) const;
    bool pointInMessageRoot(cocos2d::CCPoint const& point) const;
    std::optional<ActionHit> actionAt(cocos2d::CCPoint const& point) const;
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
    std::deque<RenderedMessage> m_history;
    std::vector<MessageHit> m_messageHits;
    std::vector<TabHit> m_tabHits;
    std::vector<ActionHit> m_actionHits;
    std::vector<ChatBan> m_bans;
    std::vector<ChatMute> m_mutes;
    std::vector<ChatBlock> m_blocks;
    std::vector<ChatReport> m_reports;
    std::unique_ptr<RateLimiter> m_rateLimiter;
    cocos2d::CCPoint m_bubblePosition = {0.0f, 0.0f};
    cocos2d::CCPoint m_panelPosition = {0.0f, 0.0f};
    cocos2d::CCPoint m_inputHitOrigin = {0.0f, 0.0f};
    cocos2d::CCSize m_inputHitSize = {0.0f, 0.0f};
    cocos2d::CCPoint m_touchStart = {0.0f, 0.0f};
    cocos2d::CCPoint m_dragStart = {0.0f, 0.0f};
    std::int64_t m_pressedAccountId = 0;
    ActionHit m_pressedAction;
    DragMode m_dragMode = DragMode::None;
    ViewMode m_viewMode = ViewMode::Chat;
    ViewMode m_previousViewMode = ViewMode::Chat;
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
    float m_listScroll = 0.0f;
    int m_touchPriorityRetries = 0;
    std::string m_joinedLevelKey;
    std::string m_selectedReportTarget;
    std::int64_t m_selectedReportAccountId = 0;
};

ComsPlusChatOverlay* activeChatOverlay();
void collapseActiveChatOverlay();
void toggleActiveChatOverlay();
void raiseActiveChatOverlay();

} // namespace comsplus
