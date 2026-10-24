// Screen Text 2 - select any area of the screen and copy its text, using the OCR engine built into Windows 10/11.
// Ctrl+Alt+T = capture, Ctrl+Alt+Shift+T = same area again, Ctrl+V = read an image from the clipboard.
// No internet, no extra downloads. The engine-independent pipeline lives in ocrcore.h.
#include "../common/ui.h"
#include <inspectable.h>
#include <winstring.h>
#include <thread>
#include <atomic>
#include <cmath>
#include "ocrcore.h"
using namespace ui;

// ---------------- minimal WinRT ABI declarations (Windows.Media.Ocr + SoftwareBitmap) ----------------
struct IAsyncInfoRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32*) = 0; virtual HRESULT STDMETHODCALLTYPE get_Status(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT*) = 0; virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};
struct IAsyncOpRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void*) = 0; virtual HRESULT STDMETHODCALLTYPE get_Completed(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(IInspectable**) = 0;
};
struct IVectorViewRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32, IInspectable**) = 0; virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32*) = 0;
};
struct ILanguageRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_LanguageTag(HSTRING*) = 0; virtual HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING*) = 0;
};
struct IOcrEngineRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RecognizeAsync(IInspectable* bmp, IAsyncOpRaw** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RecognizerLanguage(ILanguageRaw**) = 0;
};
struct IOcrEngineStaticsRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MaxImageDimension(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AvailableRecognizerLanguages(IVectorViewRaw**) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsLanguageSupported(IInspectable*, boolean*) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromLanguage(IInspectable*, IOcrEngineRaw**) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryCreateFromUserProfileLanguages(IOcrEngineRaw**) = 0;
};
struct IOcrResultRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Lines(IVectorViewRaw**) = 0; virtual HRESULT STDMETHODCALLTYPE get_TextAngle(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING*) = 0;
};
struct IOcrLineRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Words(IVectorViewRaw**) = 0; virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING*) = 0;
};
struct RectF { float X, Y, Width, Height; };
struct IOcrWordRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_BoundingRect(RectF*) = 0; virtual HRESULT STDMETHODCALLTYPE get_Text(HSTRING*) = 0;
};
struct ISoftwareBitmapFactoryRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Create(int fmt, INT32 w, INT32 h, IInspectable** out) = 0;
};
struct ISoftwareBitmapRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_BitmapPixelFormat(int*) = 0; virtual HRESULT STDMETHODCALLTYPE get_BitmapAlphaMode(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PixelWidth(INT32*) = 0; virtual HRESULT STDMETHODCALLTYPE get_PixelHeight(INT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsReadOnly(boolean*) = 0; virtual HRESULT STDMETHODCALLTYPE put_DpiX(double) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DpiX(double*) = 0; virtual HRESULT STDMETHODCALLTYPE put_DpiY(double) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DpiY(double*) = 0; virtual HRESULT STDMETHODCALLTYPE LockBuffer(int mode, IInspectable** buf) = 0;
};
struct PlaneDesc { INT32 StartIndex, Width, Height, Stride; };
struct IBitmapBufferRaw : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetPlaneCount(INT32*) = 0; virtual HRESULT STDMETHODCALLTYPE GetPlaneDescription(INT32, PlaneDesc*) = 0;
};
struct IMemoryBufferRaw : IInspectable { virtual HRESULT STDMETHODCALLTYPE CreateReference(IInspectable**) = 0; };
struct IMemoryBufferByteAccessRaw : IUnknown { virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE**, UINT32*) = 0; };
struct IClosableRaw : IInspectable { virtual HRESULT STDMETHODCALLTYPE Close() = 0; };

static const GUID IID_OcrStatics = {0x5BFFA85A, 0x3384, 0x3540, {0x99, 0x40, 0x69, 0x91, 0x20, 0xD4, 0x28, 0xA8}};
static const GUID IID_OcrLine = {0x0043A16F, 0xE31F, 0x3A24, {0x89, 0x9C, 0xD4, 0x44, 0xBD, 0x08, 0x81, 0x24}};
static const GUID IID_OcrWord = {0x3C2A477A, 0x5CD9, 0x3525, {0xBA, 0x2A, 0x23, 0xD1, 0xE0, 0xA6, 0x8A, 0x1D}};
static const GUID IID_Language = {0xEA79A752, 0xF7C2, 0x4265, {0xB1, 0xBD, 0xC4, 0xDE, 0xC4, 0xE4, 0xF0, 0x80}};
static const GUID IID_OcrResult = {0x9BD235B2, 0x175B, 0x3D6A, {0x92, 0xE2, 0x38, 0x8C, 0x20, 0x6E, 0x2F, 0x63}};
static const GUID IID_SBFactory = {0xc99feb69, 0x2d62, 0x4d47, {0xa6, 0xb3, 0x4f, 0xdb, 0x6a, 0x07, 0xfd, 0xf8}};
static const GUID IID_SB = {0x689e0708, 0x7eef, 0x483f, {0x96, 0x3f, 0xda, 0x93, 0x88, 0x18, 0xe0, 0x73}};
static const GUID IID_BitmapBuffer = {0xa53e04c4, 0x399c, 0x438c, {0xb2, 0x8f, 0xa6, 0x3a, 0x6b, 0x83, 0xd1, 0xa1}};
static const GUID IID_MemBuf = {0xfbc4dd2a, 0x245b, 0x11e4, {0xaf, 0x98, 0x68, 0x94, 0x23, 0x26, 0x0c, 0xf8}};
static const GUID IID_ByteAccess = {0x5b0d3235, 0x4dba, 0x4d44, {0x86, 0x5e, 0x8f, 0x1d, 0x0e, 0x4f, 0xd0, 0x4d}};
static const GUID IID_AsyncInfo = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_Closable = {0x30D5A829, 0x7FA4, 0x4026, {0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E}};

// combase functions loaded at runtime (so the exe still starts on systems without WinRT)
static HRESULT(WINAPI* pRoInitialize)(int);
static HRESULT(WINAPI* pRoGetActivationFactory)(HSTRING, REFIID, void**);
static HRESULT(WINAPI* pWindowsCreateString)(LPCWSTR, UINT32, HSTRING*);
static HRESULT(WINAPI* pWindowsDeleteString)(HSTRING);
static PCWSTR(WINAPI* pWindowsGetStringRawBuffer)(HSTRING, UINT32*);
static bool loadWinRT() {
    HMODULE m = LoadLibraryW(L"combase.dll"); if (!m) return false;
    pRoInitialize = (decltype(pRoInitialize))GetProcAddress(m, "RoInitialize");
    pRoGetActivationFactory = (decltype(pRoGetActivationFactory))GetProcAddress(m, "RoGetActivationFactory");
    pWindowsCreateString = (decltype(pWindowsCreateString))GetProcAddress(m, "WindowsCreateString");
    pWindowsDeleteString = (decltype(pWindowsDeleteString))GetProcAddress(m, "WindowsDeleteString");
    pWindowsGetStringRawBuffer = (decltype(pWindowsGetStringRawBuffer))GetProcAddress(m, "WindowsGetStringRawBuffer");
    return pRoInitialize && pRoGetActivationFactory && pWindowsCreateString && pWindowsDeleteString && pWindowsGetStringRawBuffer;
}
template <class T> struct Ptr { T* p = nullptr; Ptr() = default; Ptr(const Ptr&) = delete; Ptr& operator=(const Ptr&) = delete; ~Ptr() { if (p) p->Release(); } T** operator&() { return &p; } T* operator->() { return p; } explicit operator bool() const { return p; } };
static std::wstring hstr(HSTRING h) { if (!h) return L""; UINT32 n; PCWSTR s = pWindowsGetStringRawBuffer(h, &n); std::wstring r(s, n); pWindowsDeleteString(h); return r; }
static HRESULT factory(const wchar_t* cls, REFIID iid, void** out) {
    HSTRING h; pWindowsCreateString(cls, (UINT32)wcslen(cls), &h);
    HRESULT hr = pRoGetActivationFactory(h, iid, out); pWindowsDeleteString(h); return hr;
}


// ---------------- Windows OCR engine wrapper ----------------
static bool gWinRT = false;
static bool getStatics(IOcrEngineStaticsRaw** st, std::wstring& err) {
    if (!gWinRT) { err = L"Windows OCR needs Windows 10 or 11"; return false; }
    pRoInitialize(1);  // multithreaded apartment for this thread
    if (FAILED(factory(L"Windows.Media.Ocr.OcrEngine", IID_OcrStatics, (void**)st)) || !*st) { err = L"Windows OCR is not available on this PC"; return false; }
    return true;
}
static std::wstring langName(ILanguageRaw* l, std::wstring* tag = nullptr) {
    HSTRING n = nullptr, t = nullptr;
    if (tag && SUCCEEDED(l->get_LanguageTag(&t))) *tag = hstr(t);
    return SUCCEEDED(l->get_DisplayName(&n)) ? hstr(n) : L"";
}
static std::wstring engineLang(IOcrEngineRaw* e) {
    Ptr<ILanguageRaw> l; return SUCCEEDED(e->get_RecognizerLanguage(&l)) && l ? langName(l.p) : L"";
}
static const wchar_t* NO_LANG = L"No OCR language installed \x2014 Settings > Time & language > Language: add one with \x201COptical character recognition\x201D";

struct WinRecognizer : ocr::Recognizer {
    Ptr<IOcrEngineRaw> eng; Ptr<ISoftwareBitmapFactoryRaw> sbf; UINT32 md = 2600;
    int maxDim() override { return (int)md; }
    bool recognize(const ocr::Gray& g, std::vector<ocr::Line>& out, std::wstring& err) override {
        Ptr<IInspectable> sbObj; Ptr<ISoftwareBitmapRaw> sb;
        if (FAILED(sbf->Create(87 /*Bgra8*/, g.w, g.h, &sbObj)) || FAILED(sbObj->QueryInterface(IID_SB, (void**)&sb))) { err = L"Could not create bitmap for OCR"; return false; }
        {
            Ptr<IInspectable> bufObj; Ptr<IBitmapBufferRaw> bb; Ptr<IMemoryBufferRaw> mb; Ptr<IInspectable> ref; Ptr<IMemoryBufferByteAccessRaw> ba;
            BYTE* data = nullptr; UINT32 cap = 0; PlaneDesc pd{};
            if (FAILED(sb->LockBuffer(2 /*Write*/, &bufObj)) || FAILED(bufObj->QueryInterface(IID_BitmapBuffer, (void**)&bb)) ||
                FAILED(bb->GetPlaneDescription(0, &pd)) || FAILED(bufObj->QueryInterface(IID_MemBuf, (void**)&mb)) ||
                FAILED(mb->CreateReference(&ref)) || FAILED(ref->QueryInterface(IID_ByteAccess, (void**)&ba)) || FAILED(ba->GetBuffer(&data, &cap))) {
                err = L"Could not fill bitmap for OCR"; return false;
            }
            for (int y = 0; y < g.h; y++) {
                uint32_t* row = (uint32_t*)(data + pd.StartIndex + (size_t)y * pd.Stride);
                const uint8_t* s = &g.p[(size_t)y * g.w];
                for (int x = 0; x < g.w; x++) row[x] = 0xFF000000u | s[x] * 0x010101u;
            }
            Ptr<IClosableRaw> c1, c2;
            if (SUCCEEDED(ref->QueryInterface(IID_Closable, (void**)&c1))) c1->Close();
            if (SUCCEEDED(bufObj->QueryInterface(IID_Closable, (void**)&c2))) c2->Close();
        }
        Ptr<IAsyncOpRaw> op; Ptr<IAsyncInfoRaw> info;
        if (FAILED(eng->RecognizeAsync(sb.p, &op)) || FAILED(op->QueryInterface(IID_AsyncInfo, (void**)&info))) { err = L"OCR failed to start"; return false; }
        int status = 0; double t0 = nowMs();
        while (nowMs() - t0 < 30000) { info->get_Status(&status); if (status != 0) break; Sleep(1); }
        if (status == 0) { info->Cancel(); err = L"OCR timed out"; return false; }
        if (status != 1) { err = L"OCR failed"; return false; }
        Ptr<IInspectable> resObj; Ptr<IOcrResultRaw> res; Ptr<IVectorViewRaw> lines;
        if (FAILED(op->GetResults(&resObj)) || FAILED(resObj->QueryInterface(IID_OcrResult, (void**)&res)) || FAILED(res->get_Lines(&lines))) { err = L"OCR returned no result"; return false; }
        UINT32 n = 0; lines->get_Size(&n);
        for (UINT32 i = 0; i < n; i++) {
            Ptr<IInspectable> lo; Ptr<IOcrLineRaw> line; Ptr<IVectorViewRaw> words;
            if (FAILED(lines->GetAt(i, &lo)) || FAILED(lo->QueryInterface(IID_OcrLine, (void**)&line)) || FAILED(line->get_Words(&words))) continue;
            ocr::Line L; UINT32 wn = 0; words->get_Size(&wn);
            for (UINT32 k = 0; k < wn; k++) {
                Ptr<IInspectable> wo; Ptr<IOcrWordRaw> wd; RectF r{}; HSTRING t = nullptr;
                if (FAILED(words->GetAt(k, &wo)) || FAILED(wo->QueryInterface(IID_OcrWord, (void**)&wd)) || FAILED(wd->get_BoundingRect(&r)) || FAILED(wd->get_Text(&t))) continue;
                ocr::Word w; w.t = hstr(t); w.x = r.X; w.y = r.Y; w.w = r.Width; w.h = r.Height;
                if (!w.t.empty()) L.words.push_back(std::move(w));
            }
            if (!L.words.empty()) { ocr::bbox(L); out.push_back(std::move(L)); }
        }
        return true;
    }
};
// lang < 0 = the user's Windows languages, otherwise index into AvailableRecognizerLanguages
static std::unique_ptr<ocr::Recognizer> makeRecognizer(int lang, std::wstring& err) {
    Ptr<IOcrEngineStaticsRaw> st; if (!getStatics(&st, err)) return nullptr;
    auto r = std::make_unique<WinRecognizer>();
    st->get_MaxImageDimension(&r->md); if (r->md < 600) r->md = 2600;
    if (FAILED(factory(L"Windows.Graphics.Imaging.SoftwareBitmap", IID_SBFactory, (void**)&r->sbf))) { err = L"Could not create bitmap for OCR"; return nullptr; }
    if (lang >= 0) {
        Ptr<IVectorViewRaw> v; Ptr<IInspectable> lo;
        if (SUCCEEDED(st->get_AvailableRecognizerLanguages(&v)) && SUCCEEDED(v->GetAt((UINT32)lang, &lo))) st->TryCreateFromLanguage(lo.p, &r->eng);
    }
    if (!r->eng && (FAILED(st->TryCreateFromUserProfileLanguages(&r->eng)) || !r->eng)) { err = NO_LANG; return nullptr; }
    return r;
}

// ---------------- settings ----------------
enum { SEL_AUTO = -1, SEL_ALL = -2 };
struct LangInfo { std::wstring tag, name; };
static std::vector<LangInfo> gLangs;
static std::wstring gAutoName, gWantTag = L"auto";
static int gSel = SEL_AUTO, gLayout = ocr::LINES;
static bool gFast = false, gAutoCopy = true;
static std::wstring cfgPath() {
    wchar_t p[MAX_PATH]; if (!GetEnvironmentVariableW(L"APPDATA", p, MAX_PATH)) return L"";
    std::wstring d = std::wstring(p) + L"\\QRClipTools"; CreateDirectoryW(d.c_str(), nullptr); return d + L"\\screentext.txt";
}
static void loadCfg() {
    std::string s; if (!readFile(cfgPath(), s)) return;
    std::wstring t = fromUtf8(s); size_t a = 0;
    while (a < t.size()) {
        size_t e = t.find(L'\n', a); if (e == std::wstring::npos) e = t.size();
        std::wstring ln = t.substr(a, e - a); a = e + 1;
        while (!ln.empty() && (ln.back() == L'\r' || ln.back() == L' ')) ln.pop_back();
        size_t q = ln.find(L'='); if (q == std::wstring::npos) continue;
        std::wstring k = ln.substr(0, q), v = ln.substr(q + 1);
        if (k == L"lang") gWantTag = v;
        else if (k == L"layout") gLayout = std::clamp(_wtoi(v.c_str()), 0, 2);
        else if (k == L"fast") gFast = v == L"1";
        else if (k == L"autocopy") gAutoCopy = v != L"0";
    }
}
static void saveCfg() {
    std::wstring tag = gSel == SEL_ALL ? L"all" : gSel >= 0 && gSel < (int)gLangs.size() ? gLangs[gSel].tag : L"auto";
    std::string s = toUtf8(L"lang=" + tag + L"\nlayout=" + std::to_wstring(gLayout) + L"\nfast=" + (gFast ? L"1" : L"0") + L"\nautocopy=" + (gAutoCopy ? L"1" : L"0") + L"\n");
    writeFile(cfgPath(), s.data(), s.size());
}

// ---------------- UI ----------------
enum { ID_CAPTURE = 100, ID_REREAD, ID_COPY, ID_CLEAR, ID_TEXT, ID_LANG, ID_OPTS };
enum { M_AUTO = 1000, M_LANG0 = 1001, M_ALL = 1900, M_ADDLANG = 1950, M_LINES = 2000, M_PARA, M_KEEP, M_ACCURATE = 2010, M_FAST,
       M_AUTOCOPY = 2020, M_PASTE = 2030, M_REPEAT = 2040 };
enum { WM_OCRDONE = WM_APP + 1, WM_LANGS = WM_APP + 2 };
enum { HK_CAPTURE = 1, HK_REPEAT = 2 };
static HWND gWnd, gText, gCopyBtn, gCapBtn, gReBtn, gLangBtn, gOptBtn;
static std::wstring gStatus = L"Ready  \x00B7  Ctrl+Alt+T anywhere grabs text from the screen";
static COLORREF gStatusColor = MUTED;
static std::atomic<bool> gBusy{false};
static bool gLangsReady = false, gOcrErr = false;
static ocr::Gray gLastImg; static ocr::Pass gLastPass; static bool gHavePass = false;
static RECT gLastRect; static bool gHaveRect = false;

struct LangsMsg { std::vector<LangInfo> langs; std::wstring autoName, err; };
struct DoneMsg { ocr::Outcome o; double ms = 0; };

static void setStatus(const std::wstring& s, COLORREF c) { gStatus = s; gStatusColor = c; InvalidateRect(gWnd, nullptr, FALSE); }
static std::wstring selName(int sel) {
    if (sel == SEL_ALL) return L"All languages";
    if (sel >= 0 && sel < (int)gLangs.size()) return gLangs[sel].name;
    return gAutoName.empty() ? L"Auto" : L"Auto \x00B7 " + gAutoName;
}
static void updateButtons() {
    SetWindowTextW(gLangBtn, gOcrErr ? L"OCR unavailable  \x25BC" : (selName(gSel) + L"  \x25BC").c_str());
    const wchar_t* lay[] = {L"Lines", L"Paragraphs", L"Keep layout"};
    SetWindowTextW(gOptBtn, (std::wstring(lay[gLayout]) + (gFast ? L" \x00B7 Fast" : L"") + L"  \x25BC").c_str());
    EnableWindow(gCapBtn, !gBusy); EnableWindow(gReBtn, !gBusy && !gLastImg.p.empty());
}
static void showPass() {
    if (!gHavePass) return;
    std::wstring t = ocr::format(gLastPass, gLayout);
    SetWindowTextW(gText, toCrlf(t).c_str());
}
static void startOcr() {
    if (gBusy || gLastImg.p.empty()) return;
    if (!gLangsReady) { setStatus(L"Still checking Windows OCR\x2026 try again in a moment", WARN); return; }
    std::vector<int> langs;
    if (gSel == SEL_ALL) for (int i = 0; i < (int)gLangs.size(); i++) langs.push_back(i);
    else langs.push_back(gSel);
    if (langs.empty()) langs.push_back(SEL_AUTO);
    gBusy = true; updateButtons();
    setStatus(gFast ? L"Reading text\x2026" : L"Reading text (accurate mode)\x2026", MUTED);
    ocr::Gray img = gLastImg; bool fast = gFast;
    std::thread([img = std::move(img), langs, fast] {
        double t0 = nowMs();
        auto* d = new DoneMsg; d->o = ocr::runPipeline(img, langs, fast, makeRecognizer); d->ms = nowMs() - t0;
        PostMessageW(gWnd, WM_OCRDONE, 0, (LPARAM)d);
    }).detach();
}
static void setImage(const Img& im) { gLastImg = ocr::toGray(im.px.data(), im.w, im.h); startOcr(); }

static void doCapture(bool repeat) {
    if (gBusy) return;
    if (repeat && !gHaveRect) repeat = false;
    Shot s;
    if (!hideAndCapture(gWnd, s)) { bringToFront(gWnd); setStatus(L"Screen capture failed", ERR); return; }
    RECT r; bool ok;
    if (repeat) {
        r = RECT{gLastRect.left - s.x, gLastRect.top - s.y, gLastRect.right - s.x, gLastRect.bottom - s.y};
        r.left = std::max(0L, r.left); r.top = std::max(0L, r.top); r.right = std::min((LONG)s.w, r.right); r.bottom = std::min((LONG)s.h, r.bottom);
        ok = r.right - r.left > 3 && r.bottom - r.top > 3;
    } else {
        ok = selectRegion(s, r, L"Drag over the text   \x00B7   click = whole screen   \x00B7   Esc = cancel", true);
    }
    Img im; if (ok) { im = crop(s, r); gLastRect = RECT{r.left + s.x, r.top + s.y, r.right + s.x, r.bottom + s.y}; gHaveRect = true; }
    s.free();
    bringToFront(gWnd);
    if (!ok) { setStatus(repeat ? L"The last area is not on screen any more" : L"Cancelled", MUTED); return; }
    setImage(im);
}
static bool pasteImage() {
    Img im;
    if (!getClipImage(gWnd, im) || im.w < 4 || im.h < 4) { setStatus(L"No image on the clipboard", WARN); return false; }
    if (gBusy) return true;
    setImage(im); return true;
}
static bool clipHasImageOnly() {
    return !IsClipboardFormatAvailable(CF_UNICODETEXT) && (IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIBV5));
}
static void popup(HWND btn, HMENU m) {
    RECT r; GetWindowRect(btn, &r);
    TrackPopupMenu(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, r.left, r.bottom + S(4), 0, gWnd, nullptr);
    DestroyMenu(m);
}
static void langMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (gSel == SEL_AUTO ? MF_CHECKED : 0), M_AUTO, (L"Auto \x2014 Windows language" + (gAutoName.empty() ? L"" : L" (" + gAutoName + L")")).c_str());
    if (!gLangs.empty()) AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    for (int i = 0; i < (int)gLangs.size() && i < 800; i++)
        AppendMenuW(m, MF_STRING | (gSel == i ? MF_CHECKED : 0), M_LANG0 + i, (gLangs[i].name + L"   (" + gLangs[i].tag + L")").c_str());
    if (gLangs.size() > 1) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (gSel == SEL_ALL ? MF_CHECKED : 0), M_ALL, L"Try all languages, keep the best (slower)");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_ADDLANG, L"Add an OCR language\x2026");
    popup(gLangBtn, m);
}
static void optMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING | (gLayout == ocr::LINES ? MF_CHECKED : 0), M_LINES, L"Lines \x2014 keep line breaks");
    AppendMenuW(m, MF_STRING | (gLayout == ocr::PARAGRAPHS ? MF_CHECKED : 0), M_PARA, L"Paragraphs \x2014 join wrapped lines");
    AppendMenuW(m, MF_STRING | (gLayout == ocr::KEEP ? MF_CHECKED : 0), M_KEEP, L"Keep layout \x2014 tables, columns, code");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (!gFast ? MF_CHECKED : 0), M_ACCURATE, L"Accurate \x2014 several passes, best result");
    AppendMenuW(m, MF_STRING | (gFast ? MF_CHECKED : 0), M_FAST, L"Fast \x2014 single pass");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (gAutoCopy ? MF_CHECKED : 0), M_AUTOCOPY, L"Copy to clipboard automatically");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_PASTE, L"Read image from clipboard\tCtrl+V");
    AppendMenuW(m, MF_STRING | (gHaveRect ? 0 : MF_GRAYED), M_REPEAT, L"Read the same area again\tCtrl+Alt+Shift+T");
    popup(gOptBtn, m);
}
static void allowDarkMenus() {  // undocumented uxtheme ordinals used by many apps; ignored if missing
    if (HMODULE ux = LoadLibraryW(L"uxtheme.dll")) {
        auto setMode = (int(WINAPI*)(int))GetProcAddress(ux, MAKEINTRESOURCEA(135));
        auto flush = (void(WINAPI*)())GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if (setMode) setMode(2 /*ForceDark*/);
        if (flush) flush();
    }
}

static void paint(HDC dc, RECT) {
    header(dc, L"Screen Text", L"select any area   \x2192   copy its text  (offline OCR)", 196);
    fillRound(dc, R(24, 70, 712, 376), S(20), CARD);
    text(dc, L"Recognized text", R(44, 82, 200, 22), fSmall, MUTED, LEFT1);
    fillRound(dc, R(44, 112, 672, 316), S(14), FIELD, BORDER);
    text(dc, gStatus, R(26, 516, 710, 22), fSmall, gStatusColor, LEFT1 | DT_END_ELLIPSIS);
}

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        gWnd = h;
        gLangBtn = button(h, ID_LANG, L"Checking OCR\x2026", R(304, 78, 250, 28), SECONDARY, CARD);
        gOptBtn = button(h, ID_OPTS, L"Lines  \x25BC", R(562, 78, 154, 28), SECONDARY, CARD);
        gText = edit(h, ID_TEXT, R(56, 122, 652, 296), ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL);
        SendMessageW(gText, EM_SETLIMITTEXT, 8 * 1024 * 1024, 0);
        gCapBtn = button(h, ID_CAPTURE, L"Capture text   (Ctrl+Alt+T)", R(24, 458, 300, 48), PRIMARY, BG);
        gReBtn = button(h, ID_REREAD, L"Read again", R(336, 458, 124, 48), SECONDARY, BG);
        gCopyBtn = button(h, ID_COPY, L"Copy", R(472, 458, 124, 48), SECONDARY, BG);
        button(h, ID_CLEAR, L"Clear", R(608, 458, 128, 48), SECONDARY, BG);
        updateButtons();
        if (!RegisterHotKey(h, HK_CAPTURE, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T'))
            setStatus(L"Ctrl+Alt+T is used by another app \x2014 use the button instead", WARN);
        RegisterHotKey(h, HK_REPEAT, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'T');
        std::thread([] {  // find the installed OCR languages in the background
            auto* msg = new LangsMsg;
            Ptr<IOcrEngineStaticsRaw> st;
            if (getStatics(&st, msg->err)) {
                Ptr<IVectorViewRaw> v; UINT32 n = 0;
                if (SUCCEEDED(st->get_AvailableRecognizerLanguages(&v)) && v) v->get_Size(&n);
                for (UINT32 i = 0; i < n; i++) {
                    Ptr<IInspectable> o; Ptr<ILanguageRaw> lg; LangInfo li;
                    if (SUCCEEDED(v->GetAt(i, &o)) && SUCCEEDED(o->QueryInterface(IID_Language, (void**)&lg))) li.name = langName(lg.p, &li.tag);
                    if (li.name.empty()) li.name = L"Language " + std::to_wstring(i + 1);
                    msg->langs.push_back(li);
                }
                Ptr<IOcrEngineRaw> e;
                if (SUCCEEDED(st->TryCreateFromUserProfileLanguages(&e)) && e) msg->autoName = engineLang(e.p);
                else if (msg->langs.empty()) msg->err = NO_LANG;
            }
            PostMessageW(gWnd, WM_LANGS, 0, (LPARAM)msg);
        }).detach();
        return 0;
    case WM_LANGS: {
        auto* msg = (LangsMsg*)l;
        gLangs = msg->langs; gAutoName = msg->autoName; gLangsReady = true;
        gSel = SEL_AUTO;
        if (gWantTag == L"all" && gLangs.size() > 1) gSel = SEL_ALL;
        else for (int i = 0; i < (int)gLangs.size(); i++) if (_wcsicmp(gLangs[i].tag.c_str(), gWantTag.c_str()) == 0) gSel = i;
        gOcrErr = !msg->err.empty();
        if (gOcrErr) setStatus(L"\x26A0 " + msg->err, ERR);
        updateButtons();
        delete msg; return 0;
    }
    case WM_HOTKEY: if (w == HK_CAPTURE) doCapture(false); else if (w == HK_REPEAT) doCapture(true); return 0;
    case WM_OCRDONE: {
        auto* d = (DoneMsg*)l;
        gBusy = false;
        if (!d->o.ok) setStatus(d->o.err, ERR);
        else {
            gLastPass = std::move(d->o.best); gHavePass = true;
            int words = 0; for (auto& ln : gLastPass.lines) words += (int)ln.words.size();
            if (!words) {
                setStatus(gFast ? L"No text found \x2014 try Accurate mode, another language, or a bigger area" : L"No text found in that area \x2014 try another language or a bigger area", WARN);
            } else {
                showPass();
                std::wstring t = ocr::format(gLastPass, gLayout);
                bool c = gAutoCopy && setClipText(h, t);
                std::wstring ln = gLastPass.lang >= 0 && gLastPass.lang < (int)gLangs.size() ? gLangs[gLastPass.lang].name : gAutoName;
                setStatus(fmt(L"\x2713 %d characters \x00B7 %d lines%ls \x00B7 %.1f s%ls%ls", (int)t.size(), (int)gLastPass.lines.size(),
                              c ? L" \x00B7 copied" : L"", d->ms / 1000.0, ln.empty() ? L"" : L" \x00B7 ", ln.c_str()), OK);
            }
        }
        updateButtons();
        delete d; return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id >= M_LANG0 && id < M_LANG0 + (int)gLangs.size()) { gSel = id - M_LANG0; saveCfg(); updateButtons(); startOcr(); return 0; }
        switch (id) {
        case ID_CAPTURE: doCapture(false); break;
        case ID_REREAD: startOcr(); break;
        case ID_LANG: langMenu(); break;
        case ID_OPTS: optMenu(); break;
        case M_AUTO: gSel = SEL_AUTO; saveCfg(); updateButtons(); startOcr(); break;
        case M_ALL: gSel = SEL_ALL; saveCfg(); updateButtons(); startOcr(); break;
        case M_ADDLANG:
            ShellExecuteW(h, L"open", L"ms-settings:regionlanguage", nullptr, nullptr, SW_SHOWNORMAL);
            setStatus(L"Add a language, open its options and install \x201COptical character recognition\x201D, then restart Screen Text", MUTED);
            break;
        case M_LINES: case M_PARA: case M_KEEP: gLayout = id - M_LINES; saveCfg(); updateButtons(); showPass(); break;
        case M_ACCURATE: case M_FAST: gFast = id == M_FAST; saveCfg(); updateButtons(); startOcr(); break;
        case M_AUTOCOPY: gAutoCopy = !gAutoCopy; saveCfg(); break;
        case M_PASTE: pasteImage(); break;
        case M_REPEAT: doCapture(true); break;
        case ID_COPY: {
            std::wstring t = getText(gText);
            if (t.empty()) setStatus(L"Nothing to copy yet", WARN);
            else if (setClipText(h, t)) { flash(gCopyBtn, L"\x2713  Copied!"); setStatus(L"\x2713 Copied to clipboard", OK); }
            break;
        }
        case ID_CLEAR: SetWindowTextW(gText, L""); setStatus(L"Cleared", MUTED); break;
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: return ctlColor(w);
    case WM_DRAWITEM: drawButton((DRAWITEMSTRUCT*)l); return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paintBuffered(h, paint); return 0;
    case WM_DESTROY: UnregisterHotKey(h, HK_CAPTURE); UnregisterHotKey(h, HK_REPEAT); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    allowDarkMenus();
    init(inst);
    gWinRT = loadWinRT();
    loadCfg();
    HWND h = createMain(L"ScreenTextWnd", L"Screen Text", 760, 544, wndProc);
    ShowWindow(h, show); UpdateWindow(h);
    return loop([](MSG& m) {  // Ctrl+V with an image (not text) on the clipboard = read that image
        if (ctrlKey(m, 'V') && clipHasImageOnly()) { pasteImage(); return true; }
        return false;
    });
}
