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
    bool overClickable() const { return hoverAction_ != 0; }

private:
    enum Field { FNone, FMessage, FAddCode, FAddNick };
    struct Hot { Rect r; int action; };
    Host& host_;
    NetHub& hub_;
    std::vector<Hot> hots_;
    int selected_ = -1;
    Field focus_ = FNone;
    std::string msg_, addCode_, addNick_, addErr_, flash_;
    bool showAdd_ = false;
    bool showHealth_ = true;
    float mx_ = -1, my_ = -1;
    int hoverAction_ = 0;
    float scroll_ = 0;
    float chatX_ = 0, chatW_ = 0;

    int hit(float x, float y) const;
    void act(int action);
    void submitAdd();
    void sendMessage();
    std::string* focused();
    void drawColumn(Canvas& c, float h);
    void drawChat(Canvas& c, float x, float w, float h);
    void drawWelcome(Canvas& c, float x, float w, float h);
    void drawHealth(Canvas& c, float x, float h);
    void drawAdd(Canvas& c, float w, float h);
    void drawField(Canvas& c, const Rect& r, const std::string& text, const std::string& placeholder, bool focused, int action);
    void button(Canvas& c, const Rect& r, const std::string& label, Color bg, Color fg, int action);
    void hot(const Rect& r, int action);
    bool hovered(int action) const { return hoverAction_ == action; }
};

}
