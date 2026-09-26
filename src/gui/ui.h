#pragma once
#include "canvas.h"
#include "nethub.h"
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
    enum Field { FNone, FMessage, FAddCode, FAddNick, FMyName };
    enum Modal { MNone, MAdd, MSettings, MHubs, MEmoji, MMenu };
    struct Hot { Rect r; int action; std::string tip; };
    Host& host_;
    NetHub& hub_;
    std::vector<Hot> hots_;
    int selected_ = -1;
    Field focus_ = FNone;
    Modal modal_ = MNone;
    std::string msg_, addCode_, addNick_, addErr_, flash_, myName_, nameErr_;
    bool showHealth_ = false;
    std::vector<bool> callDismissed_;
    float mx_ = -1, my_ = -1;
    int hoverAction_ = 0;
    std::string hoverTip_;
    float scroll_ = 0;
    float chatX_ = 0, chatW_ = 0;

    int hit(float x, float y, std::string* tip) const;
    void act(int action);
    void submitAdd();
    void saveName();
    void sendMessage();
    std::string* focused();
    size_t capFor(Field f) const;
    void drawRail(Canvas& c, float h);
    void drawColumn(Canvas& c, float x, float h);
    void drawHome(Canvas& c, float x, float w, float h);
    void drawChat(Canvas& c, float x, float w, float h);
    void drawCallPanel(Canvas& c, float x, float w, float bottom);
    void drawHealth(Canvas& c, float x, float h);
    void drawAdd(Canvas& c, float w, float h);
    void drawSettings(Canvas& c, float w, float h);
    void drawHubs(Canvas& c, float w, float h);
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
