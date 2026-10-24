// ui.h - tiny shared dark-UI toolkit for the QR Clip tool family (Win32, header-only, C++17)
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstdarg>

namespace ui {
const COLORREF BG = RGB(15, 17, 21), CARD = RGB(24, 27, 34), FIELD = RGB(17, 20, 26),
    BORDER = RGB(42, 47, 58), TEXT = RGB(230, 232, 236), MUTED = RGB(138, 143, 152),
    ACCENT = RGB(124, 92, 255), ACCENT_H = RGB(142, 115, 255), ACCENT_P = RGB(100, 71, 240),
    BTN = RGB(37, 42, 53), BTN_H = RGB(47, 53, 66), BTN_P = RGB(56, 63, 78),
    OK = RGB(61, 220, 132), OK_DARK = RGB(34, 160, 95), WARN = RGB(255, 176, 32),
    ERR = RGB(255, 107, 107), EMPTY = RGB(30, 33, 41);
enum Kind { SECONDARY = 0, PRIMARY = 1, TOGGLE = 2 };

inline HINSTANCE inst;
inline int dpi = 96;
inline HFONT fTitle, fSub, fBody, fBtn, fBtnBold, fSmall, fEdit, fMono, fBig;
inline HBRUSH brInput;

inline int S(int v) { return MulDiv(v, dpi, 96); }
inline RECT R(int x, int y, int w, int h) { return RECT{S(x), S(y), S(x + w), S(y + h)}; }
inline double nowMs() {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / f.QuadPart;
}
inline HFONT font(int size, int weight, const wchar_t* face = L"Segoe UI") {
    return CreateFontW(-S(size), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}
inline void init(HINSTANCE h) {
    inst = h;
    HDC sdc = GetDC(nullptr); dpi = GetDeviceCaps(sdc, LOGPIXELSX); ReleaseDC(nullptr, sdc);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&icc);
    fTitle = font(26, FW_BOLD); fSub = font(14, FW_NORMAL); fBody = font(14, FW_NORMAL);
    fBtn = font(14, FW_SEMIBOLD); fBtnBold = font(15, FW_BOLD); fSmall = font(13, FW_NORMAL);
    fEdit = font(15, FW_NORMAL); fMono = font(14, FW_NORMAL, L"Consolas"); fBig = font(22, FW_BOLD, L"Consolas");
    brInput = CreateSolidBrush(FIELD);
}

// ---------- strings ----------
inline std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
inline std::wstring decode(const char* p, int len, UINT cp, DWORD flags = 0) {
    if (len <= 0) return {};
    int n = MultiByteToWideChar(cp, flags, p, len, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(n, L'\0');
    MultiByteToWideChar(cp, flags, p, len, &w[0], n);
    return w;
}
inline std::wstring fromUtf8(const std::string& s) {
    std::wstring w = decode(s.data(), (int)s.size(), CP_UTF8, MB_ERR_INVALID_CHARS);
    if (w.empty() && !s.empty()) w = decode(s.data(), (int)s.size(), CP_ACP);
    return w;
}
inline std::wstring fmt(const wchar_t* f, ...) {
    wchar_t buf[1024]; va_list a; va_start(a, f); vswprintf(buf, 1024, f, a); va_end(a); return buf;
}
inline std::wstring sizeStr(unsigned long long b) {
    if (b < 1024) return fmt(L"%llu B", b);
    if (b < 1024ull * 1024) return fmt(L"%.1f KB", b / 1024.0);
    if (b < 1024ull * 1024 * 1024) return fmt(L"%.1f MB", b / 1048576.0);
    return fmt(L"%.2f GB", b / 1073741824.0);
}
inline std::wstring baseName(const std::wstring& p) {
    size_t s = p.find_last_of(L"\\/"); return s == std::wstring::npos ? p : p.substr(s + 1);
}
inline std::wstring toCrlf(const std::wstring& t) {
    std::wstring w; w.reserve(t.size() + 64);
    for (size_t i = 0; i < t.size(); i++) { if (t[i] == L'\n' && (i == 0 || t[i - 1] != L'\r')) w += L'\r'; w += t[i]; }
    return w;
}
inline std::wstring getText(HWND h) {
    int n = GetWindowTextLengthW(h); std::vector<wchar_t> b(n + 1); GetWindowTextW(h, b.data(), n + 1); return b.data();
}

// ---------- files ----------
inline bool readFile(const std::wstring& path, std::string& out, unsigned long long maxBytes = 512ull << 20) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    if ((unsigned long long)sz.QuadPart > maxBytes) { CloseHandle(f); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0; bool ok = sz.QuadPart == 0 || ReadFile(f, &out[0], (DWORD)sz.QuadPart, &got, nullptr);
    CloseHandle(f); out.resize(got);
    return ok;
}
inline bool writeFile(const std::wstring& path, const void* data, size_t n) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; bool ok = WriteFile(f, data, (DWORD)n, &w, nullptr) && w == n;
    CloseHandle(f); return ok;
}
inline std::vector<std::wstring> openFiles(HWND owner, const wchar_t* filter, bool multi) {
    std::vector<wchar_t> buf(64 * 1024, 0);
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = owner; of.lpstrFilter = filter; of.lpstrFile = buf.data(); of.nMaxFile = (DWORD)buf.size();
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | (multi ? OFN_ALLOWMULTISELECT : 0);
    std::vector<std::wstring> r;
    if (!GetOpenFileNameW(&of)) return r;
    std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;
    if (!*p) { r.push_back(first); return r; }
    while (*p) { std::wstring n = p; r.push_back(first + L"\\" + n); p += n.size() + 1; }
    return r;
}
inline std::wstring saveFile(HWND owner, const wchar_t* filter, const wchar_t* def, const wchar_t* ext) {
    wchar_t path[MAX_PATH]; wcsncpy(path, def, MAX_PATH);
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = owner; of.lpstrFilter = filter; of.lpstrFile = path; of.nMaxFile = MAX_PATH; of.lpstrDefExt = ext;
    of.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    return GetSaveFileNameW(&of) ? path : L"";
}
inline std::vector<std::wstring> dropped(HDROP d) {
    std::vector<std::wstring> r;
    UINT n = DragQueryFileW(d, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; i++) {
        UINT len = DragQueryFileW(d, i, nullptr, 0);
        std::wstring s(len, L'\0'); DragQueryFileW(d, i, &s[0], len + 1); r.push_back(s);
    }
    DragFinish(d); return r;
}

// ---------- clipboard ----------
inline bool openClip(HWND h) { for (int i = 0; i < 25; i++) { if (OpenClipboard(h)) return true; Sleep(10); } return false; }
inline bool setClipText(HWND h, const std::wstring& t) {
    if (!openClip(h)) return false;
    EmptyClipboard();
    std::wstring c = toCrlf(t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (c.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(g), c.c_str(), (c.size() + 1) * sizeof(wchar_t)); GlobalUnlock(g);
    bool ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
    if (!ok) GlobalFree(g);
    CloseClipboard(); return ok;
}
inline std::wstring getClipText(HWND h) {
    std::wstring t;
    if (!openClip(h)) return t;
    if (HANDLE g = GetClipboardData(CF_UNICODETEXT)) if (auto* p = (const wchar_t*)GlobalLock(g)) { t = p; GlobalUnlock(g); }
    CloseClipboard(); return t;
}

// ---------- images (BGRA, 0xAARRGGBB) ----------
struct Img { int w = 0, h = 0; std::vector<uint32_t> px; };
inline bool bitmapToImg(HBITMAP hb, Img& out) {
    BITMAP bm; if (!GetObject(hb, sizeof(bm), &bm)) return false;
    out.w = bm.bmWidth; out.h = bm.bmHeight; out.px.assign((size_t)out.w * out.h, 0);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = out.w; bi.bmiHeader.biHeight = -out.h; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    int r = GetDIBits(dc, hb, 0, out.h, out.px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc); return r > 0;
}
inline bool getClipImage(HWND h, Img& out, std::wstring* filePath = nullptr) {
    if (!openClip(h)) return false;
    bool ok = false;
    if (HANDLE b = GetClipboardData(CF_BITMAP)) ok = bitmapToImg((HBITMAP)b, out);
    else if (filePath && IsClipboardFormatAvailable(CF_HDROP)) {
        if (HANDLE d = GetClipboardData(CF_HDROP)) {
            wchar_t p[MAX_PATH]; if (DragQueryFileW((HDROP)d, 0, p, MAX_PATH)) *filePath = p;
        }
    }
    CloseClipboard(); return ok;
}
// draw an Img scaled to fit inside r (keeps aspect), returns drawn rect
inline RECT drawImgFit(HDC dc, const Img& im, RECT r, bool smooth = true) {
    if (!im.w || !im.h) return r;
    int aw = r.right - r.left, ah = r.bottom - r.top;
    double s = std::min(1.0 * aw / im.w, 1.0 * ah / im.h);
    if (!smooth && s >= 1) s = (int)s;
    int w = std::max(1, (int)(im.w * s)), h = std::max(1, (int)(im.h * s));
    int x = r.left + (aw - w) / 2, y = r.top + (ah - h) / 2;
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = im.w; bi.bmiHeader.biHeight = -im.h; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, smooth && s < 1 ? HALFTONE : COLORONCOLOR); SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, x, y, w, h, 0, 0, im.w, im.h, im.px.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    return RECT{x, y, x + w, y + h};
}

// ---------- drawing ----------
inline void fillRect(HDC dc, RECT r, COLORREF c) { HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
inline void fillRound(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border) {
    HBRUSH b = CreateSolidBrush(fill); HPEN p = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(p);
}
inline void fillRound(HDC dc, RECT r, int rad, COLORREF fill) { fillRound(dc, r, rad, fill, fill); }
inline void text(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmtFlags) {
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s.c_str(), -1, &r, fmtFlags | DT_NOPREFIX);
}
const UINT LEFT1 = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
const UINT CENTER1 = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
const UINT RIGHT1 = DT_RIGHT | DT_SINGLELINE | DT_VCENTER;
inline void header(HDC dc, const wchar_t* title, const wchar_t* sub, int subX) {
    text(dc, title, R(24, 16, 400, 40), fTitle, TEXT, LEFT1);
    text(dc, sub, R(subX, 20, 760, 36), fSub, MUTED, LEFT1);
}
inline void paintBuffered(HWND h, const std::function<void(HDC, RECT)>& fn) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
    RECT cl; GetClientRect(h, &cl);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cl.right, cl.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    fillRect(mem, cl, BG);
    fn(mem, cl);
    BitBlt(dc, 0, 0, cl.right, cl.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
    EndPaint(h, &ps);
}
inline void darkTitle(HWND h) {
    BOOL d = TRUE; DwmSetWindowAttribute(h, 20, &d, sizeof(d)); DwmSetWindowAttribute(h, 19, &d, sizeof(d));
}

// ---------- buttons ----------
inline LRESULT CALLBACK btnSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_MOUSEMOVE:
        if (!GetWindowLongPtrW(h, GWLP_USERDATA)) {
            SetWindowLongPtrW(h, GWLP_USERDATA, 1);
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t);
            InvalidateRect(h, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE: SetWindowLongPtrW(h, GWLP_USERDATA, 0); InvalidateRect(h, nullptr, FALSE); break;
    case WM_SETCURSOR: if (IsWindowEnabled(h)) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; } break;
    case WM_TIMER:
        if (w == 77) {
            KillTimer(h, 77);
            if (auto* o = (std::wstring*)GetPropW(h, L"orig")) { SetWindowTextW(h, o->c_str()); delete o; RemovePropW(h, L"orig"); }
            RemovePropW(h, L"fl"); InvalidateRect(h, nullptr, FALSE); return 0;
        }
        break;
    }
    return DefSubclassProc(h, m, w, l);
}
inline HWND button(HWND parent, int id, const wchar_t* t, RECT r, int kind = SECONDARY, COLORREF bg = CARD) {
    HWND h = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, r.left, r.top,
                             r.right - r.left, r.bottom - r.top, parent, (HMENU)(INT_PTR)id, inst, nullptr);
    SetPropW(h, L"k", (HANDLE)(INT_PTR)kind);
    SetPropW(h, L"bg", (HANDLE)(INT_PTR)bg);
    SetWindowSubclass(h, btnSub, 1, 0);
    return h;
}
inline void setOn(HWND b, bool on) { SetPropW(b, L"on", (HANDLE)(INT_PTR)on); InvalidateRect(b, nullptr, FALSE); }
inline void flash(HWND b, const wchar_t* msg, int ms = 1200) {
    if (!GetPropW(b, L"orig")) SetPropW(b, L"orig", (HANDLE) new std::wstring(getText(b)));
    SetWindowTextW(b, msg); SetPropW(b, L"fl", (HANDLE)1); InvalidateRect(b, nullptr, FALSE);
    SetTimer(b, 77, ms, nullptr);
}
inline void drawButton(const DRAWITEMSTRUCT* di) {
    HDC dc = di->hDC; RECT r = di->rcItem;
    int kind = (int)(INT_PTR)GetPropW(di->hwndItem, L"k");
    bool on = GetPropW(di->hwndItem, L"on") != nullptr, fl = GetPropW(di->hwndItem, L"fl") != nullptr;
    bool pressed = di->itemState & ODS_SELECTED, dis = di->itemState & ODS_DISABLED;
    bool hover = GetWindowLongPtrW(di->hwndItem, GWLP_USERDATA) != 0 && !dis;
    fillRect(dc, r, (COLORREF)(INT_PTR)GetPropW(di->hwndItem, L"bg"));
    bool accent = kind == PRIMARY || (kind == TOGGLE && on);
    COLORREF c = accent ? (pressed ? ACCENT_P : hover ? ACCENT_H : ACCENT) : (pressed ? BTN_P : hover ? BTN_H : BTN);
    if (dis) c = BTN;
    if (fl) c = OK_DARK;
    fillRound(dc, r, S(12), c);
    std::wstring s = getText(di->hwndItem);
    text(dc, s, r, kind == PRIMARY ? fBtnBold : fBtn, dis ? MUTED : RGB(255, 255, 255), CENTER1);
}

// ---------- edits ----------
inline LRESULT CALLBACK editSub(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_CHAR && w == 1) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }  // Ctrl+A
    return DefSubclassProc(h, m, w, l);
}
inline HWND edit(HWND parent, int id, RECT r, DWORD style, HFONT f = nullptr) {
    HWND h = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | style, r.left, r.top,
                             r.right - r.left, r.bottom - r.top, parent, (HMENU)(INT_PTR)id, inst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)(f ? f : fEdit), TRUE);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
    SetWindowSubclass(h, editSub, 1, 0);
    return h;
}
inline LRESULT ctlColor(WPARAM dc) {
    SetTextColor((HDC)dc, TEXT); SetBkColor((HDC)dc, FIELD); return (LRESULT)brInput;
}

// ---------- window ----------
inline HWND createMain(const wchar_t* cls, const wchar_t* title, int cw, int ch, WNDPROC wp) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.lpfnWndProc = wp; wc.hInstance = inst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1)); wc.hIconSm = wc.hIcon; wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r = {0, 0, S(cw), S(ch)}; AdjustWindowRect(&r, style, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = wa.left + (wa.right - wa.left - ww) / 2, y = wa.top + (wa.bottom - wa.top - wh) / 2;
    HWND h = CreateWindowExW(0, cls, title, style, x, y, ww, wh, nullptr, nullptr, inst, nullptr);
    darkTitle(h);
    return h;
}
inline void bringToFront(HWND h) {
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE); else ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
}
inline int loop(const std::function<bool(MSG&)>& pre = nullptr) {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (pre && pre(msg)) continue;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
inline bool ctrlKey(const MSG& m, int vk) {
    return m.message == WM_KEYDOWN && m.wParam == (WPARAM)vk && (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000);
}

// ---------- screen capture + region select ----------
struct Shot {
    int x = 0, y = 0, w = 0, h = 0; HBITMAP bmp = nullptr; HDC dc = nullptr; uint32_t* bits = nullptr;
    void free() { if (dc) DeleteDC(dc); if (bmp) DeleteObject(bmp); dc = nullptr; bmp = nullptr; bits = nullptr; }
    uint32_t at(int sx, int sy) const {  // screen coords
        int px = sx - x, py = sy - y;
        if (px < 0 || py < 0 || px >= w || py >= h) return 0;
        return bits[(size_t)py * w + px] & 0xFFFFFF;
    }
};
inline bool capture(Shot& s) {
    s.x = GetSystemMetrics(SM_XVIRTUALSCREEN); s.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    s.w = GetSystemMetrics(SM_CXVIRTUALSCREEN); s.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC scr = GetDC(nullptr);
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = s.w; bi.bmiHeader.biHeight = -s.h; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    s.bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, (void**)&s.bits, nullptr, 0);
    if (!s.bmp) { ReleaseDC(nullptr, scr); return false; }
    s.dc = CreateCompatibleDC(scr); SelectObject(s.dc, s.bmp);
    BitBlt(s.dc, 0, 0, s.w, s.h, scr, s.x, s.y, SRCCOPY | CAPTUREBLT);
    ReleaseDC(nullptr, scr); GdiFlush();
    return true;
}
inline Img crop(const Shot& s, RECT r) {  // r in shot-local coords
    Img im; im.w = r.right - r.left; im.h = r.bottom - r.top; im.px.resize((size_t)im.w * im.h);
    for (int y = 0; y < im.h; y++) memcpy(&im.px[(size_t)y * im.w], &s.bits[(size_t)(r.top + y) * s.w + r.left], im.w * 4);
    return im;
}

struct SelState {
    Shot* s; HDC dimDC = nullptr; HBITMAP dimBmp = nullptr; HDC back = nullptr; HBITMAP backBmp = nullptr;
    POINT a{}, b{}; bool drag = false, done = false, ok = false, clickFull = false; const wchar_t* hint = L"";
    RECT sel() const { return RECT{std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x) + 1, std::max(a.y, b.y) + 1}; }
};
inline LRESULT CALLBACK selProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = (SelState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    auto inval = [&](RECT r) { InflateRect(&r, S(4), S(30)); InvalidateRect(h, &r, FALSE); };
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT p = ps.rcPaint;
        BitBlt(st->back, p.left, p.top, p.right - p.left, p.bottom - p.top, st->dimDC, p.left, p.top, SRCCOPY);
        if (st->drag) {
            RECT r = st->sel();
            BitBlt(st->back, r.left, r.top, r.right - r.left, r.bottom - r.top, st->s->dc, r.left, r.top, SRCCOPY);
            HBRUSH br = CreateSolidBrush(ACCENT); FrameRect(st->back, &r, br);
            RECT r2 = r; InflateRect(&r2, 1, 1); FrameRect(st->back, &r2, br); DeleteObject(br);
            std::wstring lab = fmt(L" %d \x00D7 %d ", (int)(r.right - r.left), (int)(r.bottom - r.top));
            RECT lr = {r.left, r.top - S(24), r.left + S(110), r.top - S(4)};
            if (lr.top < 0) { lr.top = r.bottom + S(4); lr.bottom = lr.top + S(20); }
            fillRound(st->back, lr, S(8), ACCENT);
            text(st->back, lab, lr, fSmall, RGB(255, 255, 255), CENTER1);
        } else {
            // hint pill at top of the primary monitor
            int cx = -st->s->x + GetSystemMetrics(SM_CXSCREEN) / 2, cy = -st->s->y + S(40);
            RECT hr = {cx - S(230), cy, cx + S(230), cy + S(38)};
            fillRound(st->back, hr, S(18), CARD, BORDER);
            text(st->back, st->hint, hr, fBody, TEXT, CENTER1);
        }
        BitBlt(dc, p.left, p.top, p.right - p.left, p.bottom - p.top, st->back, p.left, p.top, SRCCOPY);
        EndPaint(h, &ps); return 0;
    }
    case WM_LBUTTONDOWN:
        st->a = st->b = POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; st->drag = true; SetCapture(h);
        InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE:
        if (st->drag) { RECT o = st->sel(); st->b = POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)}; RECT n = st->sel(); RECT u; UnionRect(&u, &o, &n); inval(u); }
        return 0;
    case WM_LBUTTONUP: {
        if (!st->drag) return 0;
        ReleaseCapture(); st->b = POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        RECT r = st->sel();
        st->ok = (r.right - r.left > 3 && r.bottom - r.top > 3) || st->clickFull;
        st->done = true; return 0;
    }
    case WM_RBUTTONDOWN: st->done = true; return 0;
    case WM_KEYDOWN: if (w == VK_ESCAPE) st->done = true; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
// Fullscreen drag-to-select. Returns rect in shot-local coords. clickFull: a plain click selects the whole screen.
inline bool selectRegion(Shot& s, RECT& out, const wchar_t* hint, bool clickFull = false) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc = {sizeof(wc)}; wc.lpfnWndProc = selProc; wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS); wc.lpszClassName = L"UiRegionSelect";
        RegisterClassExW(&wc); reg = true;
    }
    SelState st; st.s = &s; st.hint = hint; st.clickFull = clickFull;
    HDC scr = GetDC(nullptr);
    // dimmed copy
    BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = s.w; bi.bmiHeader.biHeight = -s.h; bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    uint32_t* dbits = nullptr;
    st.dimBmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, (void**)&dbits, nullptr, 0);
    size_t n = (size_t)s.w * s.h;
    for (size_t i = 0; i < n; i++) { uint32_t p = s.bits[i]; dbits[i] = ((p >> 2) & 0x3F3F3F) + ((p >> 3) & 0x1F1F1F); }
    st.dimDC = CreateCompatibleDC(scr); SelectObject(st.dimDC, st.dimBmp);
    st.backBmp = CreateCompatibleBitmap(scr, s.w, s.h); st.back = CreateCompatibleDC(scr); SelectObject(st.back, st.backBmp);
    ReleaseDC(nullptr, scr);

    HWND h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"UiRegionSelect", L"", WS_POPUP, s.x, s.y, s.w, s.h,
                             nullptr, nullptr, inst, nullptr);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)&st);
    ShowWindow(h, SW_SHOW); SetForegroundWindow(h); SetFocus(h);
    MSG m;
    while (!st.done && GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    if (!st.done) PostQuitMessage(0);
    DestroyWindow(h);
    DeleteDC(st.dimDC); DeleteObject(st.dimBmp); DeleteDC(st.back); DeleteObject(st.backBmp);
    if (!st.ok) return false;
    RECT r = st.sel();
    if (r.right - r.left <= 3 || r.bottom - r.top <= 3) r = RECT{0, 0, s.w, s.h};
    r.left = std::max(0L, r.left); r.top = std::max(0L, r.top);
    r.right = std::min((LONG)s.w, r.right); r.bottom = std::min((LONG)s.h, r.bottom);
    out = r; return true;
}
// Hide our window, wait for it to vanish, capture the screen.
inline bool hideAndCapture(HWND h, Shot& s) {
    bool vis = IsWindowVisible(h) && !IsIconic(h);
    if (vis) { ShowWindow(h, SW_HIDE); DwmFlush(); Sleep(160); }
    return capture(s);
}
}  // namespace ui
