#pragma once
#include "canvas.h"
#include "../nethub.h"
#include <functional>
#include <string>
#include <vector>

namespace sc {

enum class Key { Enter, Backspace, Escape, Tab, Paste };

class StarUI {
public:
    StarUI(Host& host, NetHub& hub);
    void paint(Canvas& c, float w, float h);
    void mouseMove(float x, float y);
    void mouseDown(float x, float y);
    void wheel(float x, float y, float delta);
    void charInput(uint32_t cp);
    void key(Key k);
    bool overClickable() const { return hoverAction_ > 0; }

private:
    enum Field { FNone, FMessage, FAddCode, FAddNick, FMyName, FHubName, FInvite, FRename, FChanName };
    enum Modal { MNone, MAdd, MSettings, MHubs, MEmoji, MMenu, MHubMenu, MInvite, MHubSettings, MLeave };
    struct Hot { Rect r; int action; std::string tip; };
    Host& host_;
    NetHub& hub_;
    std::vector<Hot> hots_;
    int selected_ = -1;
    std::string selHub_;
    uint64_t selChan_ = 0;
    Field focus_ = FNone;
    Modal modal_ = MNone;
    std::string msg_, addCode_, addNick_, addErr_, flash_, myName_, nameErr_;
    std::string hubName_, invite_, rename_, chanName_;
    bool chanVoice_ = false;
    bool showHealth_ = false;
    bool showMembers_ = false;
    bool flashOk_ = false;
    int confirm_ = 0;
    uint8_t confirmKey_[32] = {0};
    uint64_t confirmChan_ = 0;
    bool wantNewHub_ = false;
    std::vector<std::string> knownHubs_;
    std::vector<bool> callDismissed_;
    float mx_ = -1, my_ = -1;
    int hoverAction_ = 0;
    std::string hoverTip_;
    float scroll_ = 0, railScroll_ = 0, colScroll_ = 0, setScroll_ = 0;
    float chatX_ = 0, chatW_ = 0;

    int hit(float x, float y, std::string* tip) const;
    void act(int action);
    bool actHub(int action);
    void submitAdd();
    void saveName();
    void sendMessage();
    std::string* focused();
    size_t capFor(Field f) const;
    HubState* curHub();
    void selectHub(const std::string& id);
    void selectChannel(HubState& hs, uint64_t channel);
    bool anyDmCall();
    void drawRail(Canvas& c, float h);
    void drawColumn(Canvas& c, float x, float h);
    void drawMePanel(Canvas& c, float x, float h);
    void drawHubColumn(Canvas& c, float x, float h, HubState& hs);
    void drawHome(Canvas& c, float x, float w, float h);
    void drawChat(Canvas& c, float x, float w, float h);
    void drawHubChat(Canvas& c, float x, float w, float h, HubState& hs);
    void drawLines(Canvas& c, std::vector<ChatLine>& lines, float x, float w, float top, float bottom,
                   const std::function<std::string(const ChatLine&)>& whoOf, const std::function<Color(const ChatLine&)>& colourOf,
                   const std::string& emptyTitle, const std::string& emptyText);
    void drawInput(Canvas& c, float x, float w, float h, const std::string& placeholder, bool live);
    void drawCallPanel(Canvas& c, float x, float w, float bottom);
    void drawVoicePanel(Canvas& c, float x, float w, float bottom);
    void drawHealth(Canvas& c, float x, float h);
    void drawMembers(Canvas& c, float x, float h, HubState& hs);
    void drawAdd(Canvas& c, float w, float h);
    void drawSettings(Canvas& c, float w, float h);
    void drawHubs(Canvas& c, float w, float h);
    void drawHubMenu(Canvas& c);
    void drawInvite(Canvas& c, float w, float h);
    void drawHubSettings(Canvas& c, float w, float h);
    void drawLeave(Canvas& c, float w, float h);
    void drawEmoji(Canvas& c, float w, float h);
    void drawMenu(Canvas& c);
    void drawTip(Canvas& c, float w, float h);
    void drawField(Canvas& c, const Rect& r, const std::string& text, const std::string& placeholder, bool focused, int action);
    void button(Canvas& c, const Rect& r, const std::string& label, Color bg, Color fg, int action, const std::string& tip = std::string());
    void iconButton(Canvas& c, const Rect& r, uint32_t glyph, Color fg, int action, const std::string& tip, bool active = false, bool slashed = false);
    void hot(const Rect& r, int action, const std::string& tip = std::string());
    bool hovered(int action) const { return hoverAction_ == action; }
};

}
