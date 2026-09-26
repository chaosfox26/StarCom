#pragma once
#include <cstdint>
#include <string>

namespace sc {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
};

inline Color rgb(uint32_t hex, float a = 1.0f){
    Color c;
    c.r = ((hex >> 16) & 255) / 255.0f;
    c.g = ((hex >> 8) & 255) / 255.0f;
    c.b = (hex & 255) / 255.0f;
    c.a = a;
    return c;
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

inline Rect rc(float x, float y, float w, float h){ Rect r; r.x = x; r.y = y; r.w = w; r.h = h; return r; }

enum class Font { Body, Small, Bold, Title, Big, Icon };
enum class Align { Left, Center, Right };

class Canvas {
public:
    virtual ~Canvas() {}
    virtual void fill(const Rect& r, Color c) = 0;
    virtual void round(const Rect& r, float radius, Color c) = 0;
    virtual void outline(const Rect& r, float radius, float width, Color c) = 0;
    virtual void circle(float cx, float cy, float radius, Color c) = 0;
    virtual void line(float x1, float y1, float x2, float y2, float width, Color c) = 0;
    virtual void text(const std::string& utf8, const Rect& r, Font f, Color c, Align a, bool wrap, bool vcenter) = 0;
    virtual float textHeight(const std::string& utf8, float maxWidth, Font f) = 0;
    virtual float textWidth(const std::string& utf8, Font f) = 0;
    virtual void clip(const Rect& r) = 0;
    virtual void unclip() = 0;
};

class Host {
public:
    virtual ~Host() {}
    virtual void copyText(const std::string& utf8) = 0;
    virtual std::string pasteText() = 0;
    virtual void redraw() = 0;
};

}
