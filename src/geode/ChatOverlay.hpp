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
    void tick(float dt);
    void onSend(cocos2d::CCObject*);

private:
    struct RenderedMessage {
        ChatMessage message;
        bool local = false;
    };

    void rebuild();
    void appendMessage(ChatMessage message, bool local);
    ChatMessage makeLocalMessage(std::string text) const;
    cocos2d::CCNode* createIconNode(std::string const& iconData) const;

    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLayerColor* m_background = nullptr;
    cocos2d::CCNode* m_messageRoot = nullptr;
    std::deque<RenderedMessage> m_messages;
    std::unique_ptr<RateLimiter> m_rateLimiter;
    float m_elapsed = 0.0f;
};

} // namespace comsplus
