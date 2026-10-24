#include "../screentext/ocrcore.h"
#include <cstdio>
#include <cstdlib>
#include <clocale>
#include <atomic>
#include <map>
#include <tuple>
#include <unistd.h>
using namespace ocr;
static std::atomic<int> cnt{0};
static int MAXD = 2600;
struct Tess : Recognizer {
    int maxDim() override { return MAXD; }
    bool recognize(const Gray& g, std::vector<Line>& out, std::wstring& err) override {
        char fn[64]; snprintf(fn, 64, "/tmp/t%d_%d.pgm", getpid(), cnt++);
        FILE* f = fopen(fn, "wb"); fprintf(f, "P5 %d %d 255\n", g.w, g.h); fwrite(g.p.data(), 1, g.p.size(), f); fclose(f);
        std::string cmd = std::string("OMP_THREAD_LIMIT=1 tesseract ") + fn + " - --psm 3 tsv 2>/dev/null";
        FILE* p = popen(cmd.c_str(), "r"); if (!p) return false;
        char buf[4096]; std::map<std::tuple<int,int,int>, int> idx;
        while (fgets(buf, sizeof buf, p)) {
            int lv, pg, bl, pa, li, wn, x, y, w, h; float conf; char txt[2048] = "";
            if (sscanf(buf, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%f\t%2047[^\n]", &lv, &pg, &bl, &pa, &li, &wn, &x, &y, &w, &h, &conf, txt) < 12) continue;
            if (lv != 5 || conf < 0) continue;
            std::string t = txt; while (!t.empty() && (t.back() == ' ' || t.back() == '\r')) t.pop_back(); if (t.empty()) continue;
            wchar_t wb[2048]; size_t n = mbstowcs(wb, t.c_str(), 2047); if (n == (size_t)-1) continue;
            auto k = std::make_tuple(bl, pa, li);
            if (!idx.count(k)) { idx[k] = (int)out.size(); out.emplace_back(); }
            Word wd; wd.t.assign(wb, n); wd.x = x; wd.y = y; wd.w = w; wd.h = h; out[idx[k]].words.push_back(wd);
        }
        pclose(p); unlink(fn); return true;
    }
};
static Gray loadPPM(const char* fn, std::vector<uint32_t>& px) {  // P6
    FILE* f = fopen(fn, "rb"); int w, h, m; fscanf(f, "P6 %d %d %d", &w, &h, &m); fgetc(f);
    std::vector<uint8_t> b((size_t)w * h * 3); fread(b.data(), 1, b.size(), f); fclose(f);
    px.resize((size_t)w * h); for (size_t i = 0; i < px.size(); i++) px[i] = (b[i*3] << 16) | (b[i*3+1] << 8) | b[i*3+2];
    return toGray(px.data(), w, h);
}
int main(int argc, char** argv) {
    setlocale(LC_ALL, "C.UTF-8");
    std::vector<uint32_t> px; Gray g = loadPPM(argv[1], px);
    std::string mode = argv[2]; int layout = argc > 3 ? atoi(argv[3]) : 0; if (argc > 4) MAXD = atoi(argv[4]);
    RecFactory fac = [](int, std::wstring&) { return std::unique_ptr<Recognizer>(new Tess()); };
    std::wstring text; int passes = 0, tiles = 0, variant = -1; double sc = 0;
    if (mode == "v1") {  // old behaviour: raw colour luminance, 2x only if < 1000 px, single image, no padding
        Tess t; Gray im = g; bool up = g.w < 1000 && g.h < 1000; if (up) im = resize(g, g.w * 2, g.h * 2);
        if (im.w > MAXD || im.h > MAXD) { printf("ERROR: Area too large for OCR\n"); return 0; }
        std::vector<Line> ls; std::wstring e; t.recognize(im, ls, e);
        Pass P; P.lines = ls; for (auto& l : P.lines) bbox(l); text = formatLines(P); passes = 1;
    } else {
        Outcome o = runPipeline(g, {0}, mode == "fast", fac);
        if (!o.ok) { printf("ERROR\n"); return 0; }
        text = format(o.best, layout); passes = o.passes; tiles = o.best.tiles; sc = o.best.scale; variant = o.best.variant;
    }
    fprintf(stderr, "passes=%d tiles=%d scale=%.2f variant=%d\n", passes, tiles, sc, variant);
    printf("%ls\n", text.c_str());
}
