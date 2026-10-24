// ocrcore.h - engine-independent part of Screen Text: image clean-up, scaling, tiling,
// multi-pass selection and text layout. Pure C++17 (no Windows headers) so it can be unit-tested anywhere.
#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <functional>
#include <memory>
#include <thread>
#include <numeric>

namespace ocr {

struct Gray { int w = 0, h = 0; std::vector<uint8_t> p; };
struct Word { std::wstring t; float x = 0, y = 0, w = 0, h = 0; };
struct Line { std::vector<Word> words; float x0 = 0, y0 = 0, x1 = 0, y1 = 0; };
struct Pass {
    std::vector<Line> lines; double score = 0, scale = 1; int variant = 0, lang = 0, tiles = 1;
    bool ok = false; std::wstring err;
};
// One OCR engine instance. recognize() gets a white-padded tile and returns lines with word boxes in tile pixels.
struct Recognizer {
    virtual ~Recognizer() {}
    virtual bool recognize(const Gray& img, std::vector<Line>& out, std::wstring& err) = 0;
    virtual int maxDim() = 0;
};
// Creates an engine for a language index inside the calling thread (engines are made per worker thread).
using RecFactory = std::function<std::unique_ptr<Recognizer>(int lang, std::wstring& err)>;

enum Variant { PLAIN = 0, LOCAL = 1, RAW = 2 };
enum Layout { LINES = 0, PARAGRAPHS = 1, KEEP = 2 };

// ---------------- character classes ----------------
inline bool isCJK(wchar_t c) {  // scripts written without spaces between words (not Hangul)
    return (c >= 0x3040 && c <= 0x30FF) || (c >= 0x3400 && c <= 0x4DBF) || (c >= 0x4E00 && c <= 0x9FFF) ||
           (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF01 && c <= 0xFF60) || (c >= 0x3000 && c <= 0x303F);
}
inline bool isRTL(wchar_t c) { return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF); }
inline bool isWordChar(wchar_t c) {
    if (c < 128) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (c < 0xC0) return false;
    if ((c >= 0x2000 && c <= 0x2BFF) || (c >= 0x3000 && c <= 0x303F) || (c >= 0xFE10 && c <= 0xFE6F)) return false;
    return c != 0xD7 && c != 0xF7;
}
inline bool isLower(wchar_t c) { return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || c == 0x131 || c == 0x11F || c == 0x15F; }
inline int dispLen(const std::wstring& s) { int n = 0; for (wchar_t c : s) n += isCJK(c) ? 2 : 1; return n; }

// ---------------- image preparation ----------------
inline Gray toGray(const uint32_t* px, int w, int h) {  // px = 0x??RRGGBB
    Gray g; g.w = w; g.h = h; g.p.resize((size_t)w * h);
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
        uint32_t v = px[i];
        g.p[i] = (uint8_t)((((v >> 16) & 255) * 77 + ((v >> 8) & 255) * 150 + (v & 255) * 29) >> 8);
    }
    return g;
}
// Dark text on a light background with full contrast (Windows OCR is most reliable on that).
inline Gray prepPlain(const Gray& s) {
    Gray g = s; size_t n = g.p.size(); if (!n) return g;
    std::vector<size_t> hist(256, 0); for (uint8_t v : g.p) hist[v]++;
    int mode = 0; size_t best = 0;
    for (int i = 0; i < 256; i++) { size_t a = 0; for (int k = std::max(0, i - 3); k <= std::min(255, i + 3); k++) a += hist[k]; if (a > best) { best = a; mode = i; } }
    double inkSum = 0; size_t inkN = 0;  // "ink" = pixels clearly different from the background
    for (int i = 0; i < 256; i++) if (std::abs(i - mode) > 40) { inkSum += (double)i * hist[i]; inkN += hist[i]; }
    if (inkN && inkSum / inkN > mode) for (auto& v : g.p) v = 255 - v;  // light text -> invert
    std::fill(hist.begin(), hist.end(), 0); for (uint8_t v : g.p) hist[v]++;
    size_t acc = 0; int lo = 0, hi = 255;
    for (int i = 0; i < 256; i++) { acc += hist[i]; if (acc > n / 200) { lo = i; break; } }
    acc = 0; for (int i = 255; i >= 0; i--) { acc += hist[i]; if (acc > n / 200) { hi = i; break; } }
    if (hi - lo >= 24 && (lo > 0 || hi < 255)) {
        uint8_t lut[256]; for (int i = 0; i < 256; i++) lut[i] = (uint8_t)std::clamp((i - lo) * 255 / (hi - lo), 0, 255);
        for (auto& v : g.p) v = lut[v];
    }
    return g;
}
// Local background removal: every pixel becomes "how different from its surroundings" -> dark ink on white,
// regardless of colours. Handles mixed light/dark areas (buttons, highlighted rows, coloured panels).
inline Gray prepLocal(const Gray& s, int r) {
    Gray g; g.w = s.w; g.h = s.h; g.p.resize(s.p.size()); if (!s.w || !s.h) return g;
    int W = s.w, H = s.h; std::vector<uint32_t> I((size_t)(W + 1) * (H + 1), 0);
    for (int y = 0; y < H; y++) { uint32_t row = 0; for (int x = 0; x < W; x++) { row += s.p[(size_t)y * W + x]; I[(size_t)(y + 1) * (W + 1) + x + 1] = I[(size_t)y * (W + 1) + x + 1] + row; } }
    std::vector<uint8_t> d(s.p.size()); std::vector<size_t> hist(256, 0);
    for (int y = 0; y < H; y++) {
        int y0 = std::max(0, y - r), y1 = std::min(H, y + r + 1);
        for (int x = 0; x < W; x++) {
            int x0 = std::max(0, x - r), x1 = std::min(W, x + r + 1);
            uint32_t sum = I[(size_t)y1 * (W + 1) + x1] - I[(size_t)y0 * (W + 1) + x1] - I[(size_t)y1 * (W + 1) + x0] + I[(size_t)y0 * (W + 1) + x0];
            int mean = (int)(sum / (uint32_t)((x1 - x0) * (y1 - y0)));
            int v = std::abs((int)s.p[(size_t)y * W + x] - mean);
            d[(size_t)y * W + x] = (uint8_t)v; hist[v]++;
        }
    }
    size_t strong = 0; for (int i = 12; i < 256; i++) strong += hist[i];
    int p98 = 255; size_t acc = 0;
    for (int i = 255; i >= 12; i--) { acc += hist[i]; if (acc > strong / 50) { p98 = i; break; } }
    double gain = 255.0 / std::max(p98, 32);
    for (size_t i = 0; i < d.size(); i++) { int v = d[i] < 5 ? 0 : (int)(d[i] * gain); g.p[i] = (uint8_t)(255 - std::min(255, v)); }
    return g;
}
// Separable Catmull-Rom resampling (sharp upscaling, anti-aliased downscaling).
inline Gray resize(const Gray& s, int nw, int nh) {
    if (nw == s.w && nh == s.h) return s;
    struct C { int a; std::vector<float> w; };
    auto kern = [](float x) { x = std::fabs(x); if (x < 1) return 1.5f * x * x * x - 2.5f * x * x + 1; if (x < 2) return -0.5f * x * x * x + 2.5f * x * x - 4 * x + 2; return 0.f; };
    auto build = [&](int sn, int dn) {
        std::vector<C> cs(dn); float sc = (float)dn / sn, fs = std::max(1.f, 1 / sc), sup = 2 * fs;
        for (int i = 0; i < dn; i++) {
            float c = (i + 0.5f) / sc; int a = (int)std::floor(c - sup), b = (int)std::ceil(c + sup);
            cs[i].a = a; float tot = 0;
            for (int j = a; j <= b; j++) { float wv = kern((j + 0.5f - c) / fs); cs[i].w.push_back(wv); tot += wv; }
            for (auto& wv : cs[i].w) wv /= tot;
        }
        return cs;
    };
    auto cx = build(s.w, nw), cy = build(s.h, nh);
    std::vector<float> tmp((size_t)s.h * nw);
    for (int y = 0; y < s.h; y++) {
        const uint8_t* row = &s.p[(size_t)y * s.w];
        for (int x = 0; x < nw; x++) {
            float v = 0; const C& c = cx[x];
            for (size_t k = 0; k < c.w.size(); k++) v += c.w[k] * row[std::clamp(c.a + (int)k, 0, s.w - 1)];
            tmp[(size_t)y * nw + x] = v;
        }
    }
    Gray r; r.w = nw; r.h = nh; r.p.resize((size_t)nw * nh);
    for (int y = 0; y < nh; y++) {
        const C& c = cy[y];
        for (int x = 0; x < nw; x++) {
            float v = 0;
            for (size_t k = 0; k < c.w.size(); k++) v += c.w[k] * tmp[(size_t)std::clamp(c.a + (int)k, 0, s.h - 1) * nw + x];
            r.p[(size_t)y * nw + x] = (uint8_t)std::clamp((int)std::lround(v), 0, 255);
        }
    }
    return r;
}

// ---------------- geometry helpers ----------------
inline void bbox(Line& l) {
    l.x0 = l.y0 = 1e30f; l.x1 = l.y1 = -1e30f;
    for (auto& w : l.words) { l.x0 = std::min(l.x0, w.x); l.y0 = std::min(l.y0, w.y); l.x1 = std::max(l.x1, w.x + w.w); l.y1 = std::max(l.y1, w.y + w.h); }
}
inline float median(std::vector<float> v) { if (v.empty()) return 0; std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); return v[v.size() / 2]; }
inline float medianLineH(const Pass& p) { std::vector<float> v; for (auto& l : p.lines) v.push_back(l.y1 - l.y0); return median(v); }
inline bool lineRTL(const Line& l) { int r = 0, t = 0; for (auto& w : l.words) for (wchar_t c : w.t) { if (isRTL(c)) r++; if (isWordChar(c)) t++; } return t && r * 2 > t; }
inline void sortWords(Line& l) {
    bool rtl = lineRTL(l);
    std::stable_sort(l.words.begin(), l.words.end(), [rtl](const Word& a, const Word& b) { return rtl ? a.x > b.x : a.x < b.x; });
}
// reading order for lines coming from several tiles: rows top to bottom, then left to right
inline void orderRows(std::vector<Line>& L) {
    std::sort(L.begin(), L.end(), [](const Line& a, const Line& b) { return a.y0 + a.y1 < b.y0 + b.y1; });
    std::vector<Line> out; size_t i = 0;
    while (i < L.size()) {
        size_t j = i + 1; float y0 = L[i].y0, y1 = L[i].y1;
        while (j < L.size()) { float cy = (L[j].y0 + L[j].y1) / 2; if (cy < y0 || cy > y1) break; j++; }
        std::stable_sort(L.begin() + i, L.begin() + j, [](const Line& a, const Line& b) { return a.x0 < b.x0; });
        for (size_t k = i; k < j; k++) out.push_back(std::move(L[k]));
        i = j;
    }
    L = std::move(out);
}
inline double scoreWords(const Pass& p) {
    double s = 0;
    for (auto& l : p.lines) for (auto& w : l.words) {
        int a = 0; for (wchar_t c : w.t) if (isWordChar(c)) a++;
        if (w.t.size() < 2) s += a ? 0.4 : 0;
        else s += (a * 2 >= (int)w.t.size()) ? a : a * 0.3;
    }
    return s;
}

// ---------------- one recognition pass (scale + white padding + tiling + merge) ----------------
struct Span { int pos, len; bool cutLo, cutHi; };
inline std::vector<Span> spans(int N, int C, int ov) {
    std::vector<Span> s;
    if (N <= C) { s.push_back({0, N, false, false}); return s; }
    int stride = std::max(C / 4, C - ov); std::vector<int> pos;
    for (int p = 0;; p += stride) { if (p + C >= N) { if (pos.empty() || pos.back() != N - C) pos.push_back(N - C); break; } pos.push_back(p); }
    for (size_t i = 0; i < pos.size(); i++) s.push_back({pos[i], C, i > 0, i + 1 < pos.size()});
    return s;
}
inline Pass runPass(Recognizer& rec, const Gray& src, double scale, float estLineH, int variant) {
    Pass P; P.scale = scale; P.variant = variant;
    int nw = std::max(1, (int)std::lround(src.w * scale)), nh = std::max(1, (int)std::lround(src.h * scale));
    Gray sc = resize(src, nw, nh);
    const int pad = 24; int M = std::min(std::max(rec.maxDim(), 600), 4000), Cn = M - 2 * pad;
    float est = estLineH > 0 ? estLineH * (float)scale : 30.f;
    int ovX = std::min(Cn / 2, std::max(240, (int)(est * 10))), ovY = std::min(Cn / 2, std::max(120, (int)(est * 3)));
    auto xs = spans(nw, Cn, ovX), ys = spans(nh, Cn, ovY);
    P.tiles = (int)(xs.size() * ys.size());
    struct Cand { Word w; int tile, line; bool cut, alive = true; };
    std::vector<Cand> cs; int tileId = 0;
    for (auto& ty : ys) for (auto& tx : xs) {
        Gray t; t.w = tx.len + 2 * pad; t.h = ty.len + 2 * pad; t.p.assign((size_t)t.w * t.h, 255);
        for (int y = 0; y < ty.len; y++) std::copy_n(&sc.p[(size_t)(ty.pos + y) * nw + tx.pos], tx.len, &t.p[(size_t)(y + pad) * t.w + pad]);
        std::vector<Line> ls; std::wstring err;
        if (!rec.recognize(t, ls, err)) { P.err = err.empty() ? L"OCR failed" : err; return P; }
        for (size_t li = 0; li < ls.size(); li++) for (auto& w : ls[li].words) {
            float lx = w.x - pad, ly = w.y - pad;
            bool cut = (tx.cutLo && lx <= 3) || (tx.cutHi && lx + w.w >= tx.len - 3) || (ty.cutLo && ly <= 3) || (ty.cutHi && ly + w.h >= ty.len - 3);
            Cand c; c.w = w; c.w.x = lx + tx.pos; c.w.y = ly + ty.pos; c.tile = tileId; c.line = (int)li; c.cut = cut; cs.push_back(c);
        }
        tileId++;
    }
    if (P.tiles > 1) {  // remove words seen twice in tile overlaps
        auto inter = [](const Word& a, const Word& b) {
            float w = std::min(a.x + a.w, b.x + b.w) - std::max(a.x, b.x), h = std::min(a.y + a.h, b.y + b.h) - std::max(a.y, b.y);
            return (w > 0 && h > 0) ? w * h : 0.f;
        };
        for (auto& a : cs) if (a.cut) for (auto& b : cs) if (b.alive && !b.cut && b.tile != a.tile && inter(a.w, b.w) > 0.3f * a.w.w * a.w.h) { a.alive = false; break; }
        for (size_t i = 0; i < cs.size(); i++) if (cs[i].alive) for (size_t j = i + 1; j < cs.size(); j++) {
            if (!cs[j].alive || cs[j].tile == cs[i].tile) continue;
            float ai = cs[i].w.w * cs[i].w.h, aj = cs[j].w.w * cs[j].w.h;
            if (inter(cs[i].w, cs[j].w) > 0.5f * std::min(ai, aj)) {
                bool dropI = cs[i].w.t.size() < cs[j].w.t.size() || (cs[i].w.t.size() == cs[j].w.t.size() && (cs[i].cut || ai < aj));
                (dropI ? cs[i] : cs[j]).alive = false;
                if (dropI) break;
            }
        }
    }
    // fragments per (tile,line), back to source pixels
    std::vector<Line> frag; std::vector<int> fragTile; std::vector<long long> keys;
    for (auto& c : cs) {
        if (!c.alive) continue;
        long long k = (long long)c.tile * 1000000 + c.line;
        size_t idx = std::find(keys.begin(), keys.end(), k) - keys.begin();
        if (idx == keys.size()) { keys.push_back(k); frag.emplace_back(); fragTile.push_back(c.tile); }
        Word w = c.w; w.x /= (float)scale; w.y /= (float)scale; w.w /= (float)scale; w.h /= (float)scale;
        frag[idx].words.push_back(w);
    }
    for (auto& f : frag) bbox(f);
    if (P.tiles > 1) {  // join pieces of one text line that were split by a tile border
        std::vector<int> par(frag.size()); std::iota(par.begin(), par.end(), 0);
        std::function<int(int)> find = [&](int x) { return par[x] == x ? x : par[x] = find(par[x]); };
        for (size_t i = 0; i < frag.size(); i++) for (size_t j = i + 1; j < frag.size(); j++) {
            if (fragTile[i] == fragTile[j]) continue;
            Line &a = frag[i], &b = frag[j];
            float ha = a.y1 - a.y0, hb = b.y1 - b.y0, ov = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
            float gap = std::max(a.x0, b.x0) - std::min(a.x1, b.x1);
            if (ov >= 0.5f * std::min(ha, hb) && gap < 1.5f * std::max(ha, hb)) par[find((int)i)] = find((int)j);
        }
        std::vector<Line> merged; std::vector<int> root2idx(frag.size(), -1);
        for (size_t i = 0; i < frag.size(); i++) {
            int r = find((int)i);
            if (root2idx[r] < 0) { root2idx[r] = (int)merged.size(); merged.emplace_back(); }
            auto& m = merged[root2idx[r]]; m.words.insert(m.words.end(), frag[i].words.begin(), frag[i].words.end());
        }
        for (auto& m : merged) { sortWords(m); bbox(m); }
        orderRows(merged);
        frag = std::move(merged);
    }
    for (auto& f : frag) if (!f.words.empty()) P.lines.push_back(std::move(f));
    P.ok = true; P.score = scoreWords(P);
    return P;
}

// ---------------- the full multi-pass pipeline ----------------
struct Outcome { bool ok = false; std::wstring err; Pass best; int passes = 0; };
inline double capScale(const Gray& g, double s, double maxPix) {
    double area = (double)g.w * g.h; if (area * s * s > maxPix) s = std::sqrt(maxPix / area);
    return std::max(s, 0.25);
}
// langs[0] is the main language; more entries = "try all languages". fast = fewer passes.
inline Outcome runPipeline(const Gray& base, const std::vector<int>& langs, bool fast, const RecFactory& fac) {
    Outcome O; std::wstring err;
    if (base.w < 4 || base.h < 4) { O.err = L"Area too small"; return O; }
    auto rec = fac(langs[0], err); if (!rec) { O.err = err; return O; }
    const double MAXPIX = 30e6;
    Gray plain = prepPlain(base);
    double s0 = capScale(base, base.h < 70 ? 3.0 : 2.0, MAXPIX);
    Pass best = runPass(*rec, plain, s0, 0, PLAIN); O.passes++;
    if (!best.ok) { O.err = best.err; return O; }
    if (best.lines.empty()) {  // nothing found: text may be tiny or huge - try other sizes
        for (double s : {s0 >= 2.5 ? 1.0 : 3.5, 1.25}) {
            s = capScale(base, s, MAXPIX); if (std::fabs(s / s0 - 1) < 0.2) continue;
            Pass p = runPass(*rec, plain, s, 0, PLAIN); O.passes++;
            if (p.ok && p.score > best.score) best = std::move(p);
        }
    }
    float hl = medianLineH(best);
    if (hl > 0) {  // re-run at the size where text lines are ~34 px tall (Windows OCR sweet spot)
        double sB = capScale(base, std::clamp(34.0 / hl, 0.6, 4.0), MAXPIX), ratio = sB / best.scale;
        if (fast ? (ratio > 1.6 || ratio < 0.6) : std::fabs(ratio - 1) > 0.3) {
            Pass p = runPass(*rec, plain, sB, hl, PLAIN); O.passes++;
            if (p.ok && p.score >= best.score * 0.98) best = std::move(p);
        }
    }
    rec.reset();
    hl = medianLineH(best);
    Gray local, raw;
    if (!fast) {  // two more clean-up variants in parallel, keep whichever reads the most text
        local = prepLocal(base, std::clamp((int)std::lround((hl > 0 ? hl : 16) * 1.3), 6, 80));
        raw = base;
        Pass pl, pr; double s = best.scale;
        std::thread t1([&] { std::wstring e; if (auto r = fac(langs[0], e)) pl = runPass(*r, local, s, hl, LOCAL); });
        std::thread t2([&] { std::wstring e; if (auto r = fac(langs[0], e)) pr = runPass(*r, raw, s, hl, RAW); });
        t1.join(); t2.join(); O.passes += 2;
        if (pl.ok && pl.score > best.score * 1.03) best = std::move(pl);
        if (pr.ok && pr.score > best.score * 1.03) best = std::move(pr);
    }
    best.lang = langs[0];
    if (langs.size() > 1) {  // try the other installed languages with the winning settings
        const Gray& img = best.variant == LOCAL ? local : best.variant == RAW ? raw : plain;
        std::vector<Pass> res(langs.size()); std::vector<std::thread> th;
        for (size_t i = 1; i < langs.size(); i++)
            th.emplace_back([&, i] { std::wstring e; if (auto r = fac(langs[i], e)) { res[i] = runPass(*r, img, best.scale, hl, best.variant); res[i].lang = langs[i]; } });
        for (auto& t : th) t.join(); O.passes += (int)langs.size() - 1;
        for (size_t i = 1; i < langs.size(); i++) if (res[i].ok && res[i].score > best.score * 1.05) best = std::move(res[i]);
    }
    O.best = std::move(best); O.ok = true;
    return O;
}

// ---------------- text output ----------------
inline void appendWord(std::wstring& s, const std::wstring& w) {
    if (!s.empty() && !w.empty() && !(isCJK(s.back()) && isCJK(w.front()))) s += L' ';
    s += w;
}
inline std::wstring lineText(const Line& l) { std::wstring s; for (auto& w : l.words) appendWord(s, w.t); return s; }
inline bool startsListItem(const std::wstring& t) {
    if (t.empty()) return false;
    wchar_t c = t[0];
    if (c == 0x2022 || c == 0x25CF || c == 0x25AA || c == 0x2013 || c == 0x2014 || c == L'-' || c == L'*' || c == 0x25E6) return true;
    size_t i = 0; while (i < t.size() && i < 3 && t[i] >= '0' && t[i] <= '9') i++;
    return i > 0 && i < t.size() && (t[i] == L'.' || t[i] == L')');
}
inline std::wstring formatLines(const Pass& p) {
    std::wstring o; float mh = medianLineH(p);
    for (size_t i = 0; i < p.lines.size(); i++) {
        if (i) { float gap = p.lines[i].y0 - p.lines[i - 1].y1; o += (gap > 1.0f * mh) ? L"\n\n" : L"\n"; }
        o += lineText(p.lines[i]);
    }
    return o;
}
inline std::wstring formatParagraphs(const Pass& p) {
    std::wstring o; float mh = medianLineH(p);
    float L0 = 1e30f, R0 = -1e30f; for (auto& l : p.lines) { L0 = std::min(L0, l.x0); R0 = std::max(R0, l.x1); }
    for (size_t i = 0; i < p.lines.size(); i++) {
        std::wstring t = lineText(p.lines[i]);
        if (!i) { o = t; continue; }
        const Line &a = p.lines[i - 1], &b = p.lines[i];
        float gap = b.y0 - a.y1; wchar_t last = o.empty() ? 0 : o.back();
        bool shortPrev = (R0 - a.x1) > 0.2f * (R0 - L0) && (last == L'.' || last == L'!' || last == L'?' || last == L':' || last == 0x3002);
        bool brk = gap > 0.75f * mh || gap < -0.5f * mh || (b.x0 - a.x0) > 1.5f * mh || (a.x0 - b.x0) > 3.0f * mh || shortPrev || startsListItem(t) ||
                   std::fabs((b.y1 - b.y0) - (a.y1 - a.y0)) > 0.45f * mh;
        if (brk) { o += L"\n"; o += t; continue; }
        if (last == L'-' && o.size() > 1 && isWordChar(o[o.size() - 2]) && !t.empty() && isLower(t[0])) { o.pop_back(); o += t; }
        else appendWord(o, t);
    }
    return o;
}
inline std::wstring formatKeep(const Pass& p) {  // keeps columns & indentation using spaces (tables, code)
    std::vector<Word> ws; for (auto& l : p.lines) ws.insert(ws.end(), l.words.begin(), l.words.end());
    if (ws.empty()) return L"";
    double sw = 0, sn = 0, sw1 = 0, sn1 = 0; float minX = 1e30f;
    for (auto& w : ws) { int n = dispLen(w.t); minX = std::min(minX, w.x); if (n >= 3) { sw += w.w; sn += n; } if (n) { sw1 += w.w; sn1 += n; } }
    float ch = (float)std::max(1.0, sn ? sw / sn : sn1 ? sw1 / sn1 : 8.0), mh = medianLineH(p);
    std::sort(ws.begin(), ws.end(), [](const Word& a, const Word& b) { return a.y + a.h / 2 < b.y + b.h / 2; });
    std::vector<Line> rows;
    for (auto& w : ws) {
        float cy = w.y + w.h / 2;
        if (!rows.empty() && cy >= rows.back().y0 && cy <= rows.back().y1) { rows.back().words.push_back(w); continue; }
        Line r; r.words.push_back(w); bbox(r); rows.push_back(r);
    }
    std::wstring o;
    for (size_t i = 0; i < rows.size(); i++) {
        auto& r = rows[i]; sortWords(r); bbox(r);
        if (lineRTL(r)) std::reverse(r.words.begin(), r.words.end());  // place by x left->right
        if (i) { float gap = r.y0 - rows[i - 1].y1; int blanks = std::min(2, (int)(gap / std::max(1.f, 1.5f * mh))); o += L"\n"; for (int b = 0; b < blanks; b++) o += L"\n"; }
        std::wstring s; int len = 0;
        for (auto& w : r.words) {
            int col = (int)std::lround((w.x - minX) / ch);
            if (len > 0 && col <= len) col = len + 1;
            if (len > 0 && col == len + 1 && isCJK(s.back()) && !w.t.empty() && isCJK(w.t[0])) col = len;
            s.append(col - len, L' '); len = col; s += w.t; len += dispLen(w.t);
        }
        while (!s.empty() && s.back() == L' ') s.pop_back();
        o += s;
    }
    return o;
}
inline std::wstring format(const Pass& p, int layout) {
    return layout == PARAGRAPHS ? formatParagraphs(p) : layout == KEEP ? formatKeep(p) : formatLines(p);
}
}  // namespace ocr
