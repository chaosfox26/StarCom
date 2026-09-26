#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <string>
#include <vector>
#include "canvas.h"
#include "nethub.h"
#include "ui.h"
#include "gui.h"
#include "../platform_net.h"

namespace {

using namespace sc;

std::wstring widen(const std::string& s){
    if(s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w){
    if(w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

template<class T> void release(T*& p){ if(p){ p->Release(); p = nullptr; } }

class D2DCanvas : public Canvas {
public:
    ID2D1Factory* factory = nullptr;
    IDWriteFactory* dw = nullptr;
    ID2D1HwndRenderTarget* rt = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    IDWriteTextFormat* fmt[9] = {};
    int clips = 0;

    bool init(){
        if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory))) return false;
        if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&dw))) return false;
        const struct { float size; DWRITE_FONT_WEIGHT w; const wchar_t* face; } spec[9] = {
            { 15.0f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI" }, { 12.5f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI" },
            { 15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI" }, { 20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, L"Segoe UI" },
            { 26.0f, DWRITE_FONT_WEIGHT_BOLD, L"Segoe UI" }, { 18.0f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI Emoji" },
            { 16.0f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe MDL2 Assets" }, { 22.0f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe MDL2 Assets" },
            { 11.0f, DWRITE_FONT_WEIGHT_NORMAL, L"Segoe UI" } };
        for(int i = 0; i < 9; i++){
            if(FAILED(dw->CreateTextFormat(spec[i].face, nullptr, spec[i].w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, spec[i].size, L"", &fmt[i]))) return false;
        }
        return true;
    }

    bool ensureTarget(HWND hwnd){
        if(rt) return true;
        RECT r;
        GetClientRect(hwnd, &r);
        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
        UINT dpi = GetDpiForWindow(hwnd);
        props.dpiX = props.dpiY = (float)dpi;
        if(FAILED(factory->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU((UINT32)(r.right - r.left), (UINT32)(r.bottom - r.top))), &rt))) return false;
        rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        return SUCCEEDED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush));
    }

    void dropTarget(){ release(brush); release(rt); }

    void shutdown(){
        dropTarget();
        for(auto& f : fmt) release(f);
        release(dw);
        release(factory);
    }

    ID2D1SolidColorBrush* col(Color c){ brush->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a)); return brush; }
    static D2D1_RECT_F R(const Rect& r){ return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }

    void fill(const Rect& r, Color c) override { rt->FillRectangle(R(r), col(c)); }
    void round(const Rect& r, float radius, Color c) override { rt->FillRoundedRectangle(D2D1::RoundedRect(R(r), radius, radius), col(c)); }
    void outline(const Rect& r, float radius, float width, Color c) override { rt->DrawRoundedRectangle(D2D1::RoundedRect(R(r), radius, radius), col(c), width); }
    void circle(float cx, float cy, float radius, Color c) override { rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), col(c)); }
    void line(float x1, float y1, float x2, float y2, float width, Color c) override { rt->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), col(c), width); }

    IDWriteTextLayout* layout(const std::string& utf8, float maxW, float maxH, Font f, bool wrap){
        std::wstring w = widen(utf8);
        IDWriteTextLayout* l = nullptr;
        if(FAILED(dw->CreateTextLayout(w.c_str(), (UINT32)w.size(), fmt[(int)f], maxW > 1 ? maxW : 1, maxH > 1 ? maxH : 1, &l))) return nullptr;
        l->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        return l;
    }

    void text(const std::string& utf8, const Rect& r, Font f, Color c, Align a, bool wrap, bool vcenter) override {
        IDWriteTextLayout* l = layout(utf8, r.w, r.h, f, wrap);
        if(!l) return;
        l->SetTextAlignment(a == Align::Center ? DWRITE_TEXT_ALIGNMENT_CENTER : a == Align::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
        l->SetParagraphAlignment(vcenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        if(!wrap){
            DWRITE_TRIMMING trim = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
            IDWriteInlineObject* ell = nullptr;
            if(SUCCEEDED(dw->CreateEllipsisTrimmingSign(fmt[(int)f], &ell))){ l->SetTrimming(&trim, ell); ell->Release(); }
        }
        rt->DrawTextLayout(D2D1::Point2F(r.x, r.y), l, col(c), D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT | D2D1_DRAW_TEXT_OPTIONS_CLIP);
        l->Release();
    }

    float textHeight(const std::string& utf8, float maxWidth, Font f) override {
        IDWriteTextLayout* l = layout(utf8, maxWidth, 100000, f, true);
        if(!l) return 20;
        DWRITE_TEXT_METRICS m;
        l->GetMetrics(&m);
        l->Release();
        return m.height;
    }

    float textWidth(const std::string& utf8, Font f) override {
        IDWriteTextLayout* l = layout(utf8, 100000, 1000, f, false);
        if(!l) return 0;
        DWRITE_TEXT_METRICS m;
        l->GetMetrics(&m);
        l->Release();
        return m.widthIncludingTrailingWhitespace;
    }

    void clip(const Rect& r) override { rt->PushAxisAlignedClip(R(r), D2D1_ANTIALIAS_MODE_ALIASED); clips++; }
    void unclip() override { if(clips > 0){ rt->PopAxisAlignedClip(); clips--; } }
};

const UINT WM_NETWAKE = WM_APP + 1;

class App : public Host {
public:
    HWND hwnd = nullptr;
    D2DCanvas canvas;
    NetHub hub;
    StarUI* ui = nullptr;

    void copyText(const std::string& utf8) override {
        if(!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        std::wstring w = widen(utf8);
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if(g){
            void* p = GlobalLock(g);
            if(p){ memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g); SetClipboardData(CF_UNICODETEXT, g); }
            else GlobalFree(g);
        }
        CloseClipboard();
    }

    std::string pasteText() override {
        std::string out;
        if(!OpenClipboard(hwnd)) return out;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if(h){
            const wchar_t* p = (const wchar_t*)GlobalLock(h);
            if(p){ out = narrow(p); GlobalUnlock(h); }
        }
        CloseClipboard();
        return out;
    }

    void redraw() override { if(hwnd) InvalidateRect(hwnd, nullptr, FALSE); }

    float dipScale(){ return GetDpiForWindow(hwnd) / 96.0f; }

    void paint(){
        if(!canvas.ensureTarget(hwnd)) return;
        hub.pump();
        D2D1_SIZE_F sz = canvas.rt->GetSize();
        canvas.rt->BeginDraw();
        canvas.rt->SetTransform(D2D1::Matrix3x2F::Identity());
        ui->paint(canvas, sz.width, sz.height);
        while(canvas.clips > 0) canvas.unclip();
        if(canvas.rt->EndDraw() == D2DERR_RECREATE_TARGET) canvas.dropTarget();
    }
};

App* g_app = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    App* a = g_app;
    switch(msg){
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if(a && a->ui) a->paint();
        EndPaint(hwnd, &ps);
        return 0; }
    case WM_SIZE:
        if(a && a->canvas.rt) a->canvas.rt->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        if(a) a->redraw();
        return 0;
    case WM_DPICHANGED: {
        if(a) a->canvas.dropTarget();
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_NETWAKE: if(a) a->redraw(); return 0;
    case WM_MOUSEMOVE:
        if(a && a->ui){ float s = a->dipScale(); a->ui->mouseMove(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s); }
        return 0;
    case WM_LBUTTONDOWN:
        if(a && a->ui){ float s = a->dipScale(); a->ui->mouseDown(GET_X_LPARAM(lp) / s, GET_Y_LPARAM(lp) / s); }
        return 0;
    case WM_SETCURSOR:
        if(a && a->ui && LOWORD(lp) == HTCLIENT){ SetCursor(LoadCursor(nullptr, a->ui->overClickable() ? IDC_HAND : IDC_ARROW)); return TRUE; }
        break;
    case WM_MOUSEWHEEL:
        if(a && a->ui){
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &p);
            float s = a->dipScale();
            a->ui->wheel(p.x / s, p.y / s, GET_WHEEL_DELTA_WPARAM(wp) / 120.0f * 60.0f);
        }
        return 0;
    case WM_CHAR:
        if(a && a->ui){
            static wchar_t high = 0;
            wchar_t ch = (wchar_t)wp;
            if(ch >= 0xD800 && ch <= 0xDBFF){ high = ch; return 0; }
            uint32_t cp = ch;
            if(ch >= 0xDC00 && ch <= 0xDFFF && high){ cp = 0x10000 + (((uint32_t)high - 0xD800) << 10) + ((uint32_t)ch - 0xDC00); high = 0; }
            a->ui->charInput(cp);
        }
        return 0;
    case WM_KEYDOWN:
        if(a && a->ui){
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if(wp == VK_RETURN) a->ui->key(Key::Enter);
            else if(wp == VK_BACK) a->ui->key(Key::Backspace);
            else if(wp == VK_ESCAPE) a->ui->key(Key::Escape);
            else if(wp == VK_TAB) a->ui->key(Key::Tab);
            else if(ctrl && wp == 'V') a->ui->key(Key::Paste);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* m = (MINMAXINFO*)lp;
        m->ptMinTrackSize.x = 940;
        m->ptMinTrackSize.y = 560;
        return 0; }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}

int runGui(){
    HINSTANCE inst = GetModuleHandleW(nullptr);
    int show = SW_SHOWDEFAULT;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(!sc::netInit()){ MessageBoxW(nullptr, L"Networking could not start.", L"StarCom", MB_ICONERROR); return 1; }
    App app;
    g_app = &app;
    if(!app.canvas.init()){ MessageBoxW(nullptr, L"Drawing could not start (Direct2D).", L"StarCom", MB_ICONERROR); return 1; }
    std::string err;
    if(!app.hub.start(err)){ MessageBoxW(nullptr, widen(err).c_str(), L"StarCom", MB_ICONERROR); return 1; }
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"StarComWindow";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    app.hwnd = CreateWindowExW(0, L"StarComWindow", L"StarCom", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr, nullptr, inst, nullptr);
    if(!app.hwnd) return 1;
    BOOL dark = TRUE;
    typedef HRESULT (WINAPI *DwmSet)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if(dwm){ DwmSet f = (DwmSet)GetProcAddress(dwm, "DwmSetWindowAttribute"); if(f) f(app.hwnd, 20, &dark, sizeof dark); }
    StarUI ui(app, app.hub);
    app.ui = &ui;
    HWND target = app.hwnd;
    app.hub.wake = [target]{ PostMessageW(target, WM_NETWAKE, 0, 0); };
    ShowWindow(app.hwnd, show);
    UpdateWindow(app.hwnd);
    MSG m;
    while(GetMessageW(&m, nullptr, 0, 0) > 0){ TranslateMessage(&m); DispatchMessageW(&m); }
    app.hub.shutdown();
    app.ui = nullptr;
    app.canvas.shutdown();
    if(dwm) FreeLibrary(dwm);
    sc::netShutdown();
    return 0;
}
#endif
