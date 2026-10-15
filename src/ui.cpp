#include "ui.h"
#include "config.h"
#include "player.h"
#include "browser.h"
#include <M5Unified.h>

namespace {

M5Canvas s_canvas(&M5.Display);

uint32_t s_lastDraw = 0;
uint32_t s_lastSpec = 0;
bool s_invalid = true;

uint32_t s_volToastEnd = 0;
uint8_t s_volToastVal = 0;

int s_marqueeX = 0;
uint32_t s_marqueeLast = 0;

constexpr int SPEC_N = 28;
uint8_t s_spec[SPEC_N];
uint32_t s_seed = 0x12345678;

int s_bat = -1;
uint32_t s_batNext = 0;

uint16_t lerp565(uint16_t a, uint16_t b, uint8_t t) {
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    int rr = ar + ((br - ar) * t) / 255;
    int rg = ag + ((bg - ag) * t) / 255;
    int rb = ab + ((bb - ab) * t) / 255;
    return (uint16_t)((rr << 11) | (rg << 5) | rb);
}

void fmtTime(char* out, size_t n, uint32_t sec) {
    snprintf(out, n, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}

void utf8Pop(String& s) {
    if (s.length() == 0) return;
    int i = s.length() - 1;
    while (i > 0 && (((uint8_t)s[i] & 0xC0) == 0x80)) i--;
    s.remove(i);
}

String fitLeft(const String& s, int maxW) {
    if (s_canvas.textWidth(s.c_str()) <= maxW) return s;
    String out = s;
    while (out.length() > 1 && s_canvas.textWidth(("." + out).c_str()) > maxW) utf8Pop(out);
    return "..." + out;
}

String truncRight(const String& s, int maxW) {
    if (s_canvas.textWidth(s.c_str()) <= maxW) return s;
    String out = s;
    while (out.length() > 1 && s_canvas.textWidth((out + ".").c_str()) > maxW) utf8Pop(out);
    return out + ".";
}

void drawBattery() {
    int x = 194, y = 3;
    s_canvas.drawRect(x, y, 16, 9, cfg::COL_DIM);
    s_canvas.fillRect(x + 16, y + 2, 2, 5, cfg::COL_DIM);
    char buf[8];
    if (s_bat >= 0) {
        int w = (14 * s_bat) / 100;
        if (w > 0) {
            s_canvas.fillRect(x + 1, y + 1, w, 7, s_bat > 25 ? cfg::COL_CYAN : cfg::COL_PURPLE);
        }
        snprintf(buf, sizeof(buf), "%d%%", s_bat);
    } else {
        snprintf(buf, sizeof(buf), "--%%");
    }
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.setTextColor(cfg::COL_DIM);
    s_canvas.drawString(buf, 240 - 4, 2);
    s_canvas.setTextDatum(textdatum_t::top_left);
}

void drawHeader() {
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextDatum(textdatum_t::top_left);
    s_canvas.setTextColor(cfg::COL_PURPLE);
    s_canvas.drawString("CARDPLAYER", 4, 2);
    drawBattery();
}

void drawVolToast() {
    int x = 78, y = 52, w = 84, h = 28;
    s_canvas.fillRoundRect(x, y, w, h, 4, cfg::COL_TRACK);
    s_canvas.drawRoundRect(x, y, w, h, 4, cfg::COL_PURPLE);
    char buf[16];
    snprintf(buf, sizeof(buf), "VOL %u", (unsigned)s_volToastVal);
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(cfg::COL_TEXT);
    s_canvas.setTextDatum(textdatum_t::middle_center);
    s_canvas.drawString(buf, x + w / 2, y + 9);
    s_canvas.drawRect(x + 8, y + 19, w - 16, 4, cfg::COL_DIM);
    if (s_volToastVal > 0) {
        int fw = (w - 18) * s_volToastVal / cfg::VOLUME_MAX;
        if (fw > 0) s_canvas.fillRect(x + 9, y + 20, fw, 2, cfg::COL_CYAN);
    }
    s_canvas.setTextDatum(textdatum_t::middle_center);
}

void drawNowPlaying() {
    const PlayerState& st = player::state();
    s_canvas.fillSprite(cfg::COL_BG);
    drawHeader();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(cfg::COL_DIM);
    if (st.active) {
        int i = st.path.lastIndexOf('/');
        String folder = i > 0 ? st.path.substring(0, i) : String("/sd");
        String vis = fitLeft(folder, 232);
        s_canvas.drawString(vis.c_str(), 4, 16);
    } else {
        s_canvas.drawString("Nenhuma musica - aperte B para abrir a lista", 4, 16);
    }

    s_canvas.setFont(&fonts::efontJA_16);
    String title = st.active ? st.title : String("CARDPLAYER");
    int tw = s_canvas.textWidth(title.c_str());
    int scroll = 0;
    if (tw > 232) {
        uint32_t now = millis();
        if (now - s_marqueeLast >= 40) {
            s_marqueeX += 2;
            s_marqueeLast = now;
        }
        int span = tw + 60;
        if (s_marqueeX > span) s_marqueeX = 0;
        scroll = s_marqueeX;
    } else {
        s_marqueeX = 0;
    }
    s_canvas.setTextColor(cfg::COL_TEXT);
    s_canvas.setClipRect(4, 28, 232, 22);
    s_canvas.drawString(title.c_str(), 4 - scroll, 29);
    s_canvas.clearClipRect();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(cfg::COL_CYAN);
    s_canvas.drawString(st.artist.c_str(), 4, 52);
    if (st.count > 0) {
        char idx[24];
        snprintf(idx, sizeof(idx), "%d/%d", st.index + 1, st.count);
        s_canvas.setTextDatum(textdatum_t::top_right);
        s_canvas.setTextColor(cfg::COL_PURPLE);
        s_canvas.drawString(idx, 236, 52);
        s_canvas.setTextDatum(textdatum_t::top_left);
    }

    int px = 8, py = 66, pw = 224, ph = 8;
    s_canvas.fillRoundRect(px, py, pw, ph, 4, cfg::COL_TRACK);
    float frac = 0;
    if (st.duration > 0) {
        frac = (float)st.position / (float)st.duration;
        if (frac > 1) frac = 1;
    }
    int fw = (int)(pw * frac);
    if (fw > 0) {
        s_canvas.fillRoundRect(px, py, fw, ph, 4, cfg::COL_PURPLE);
        int kx = px + fw;
        if (kx > px + pw - 4) kx = px + pw - 4;
        if (kx < px + 4) kx = px + 4;
        s_canvas.fillCircle(kx, py + ph / 2, 5, cfg::COL_CYAN);
    }

    char cur[12], rem[12];
    fmtTime(cur, sizeof(cur), st.position);
    uint32_t rd = st.duration > st.position ? st.duration - st.position : 0;
    fmtTime(rem, sizeof(rem), rd);
    s_canvas.setTextColor(cfg::COL_DIM);
    s_canvas.drawString(cur, px, 78);
    String rems = "-" + String(rem);
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.drawString(rems.c_str(), px + pw, 78);
    s_canvas.setTextDatum(textdatum_t::top_left);

    int sx = 8, sy = 120, bw = 4, gap = 4;
    for (int i = 0; i < SPEC_N; i++) {
        int h = s_spec[i];
        if (h < 2) h = 2;
        int x = sx + i * (bw + gap);
        uint8_t t = (uint8_t)(h * 8 > 255 ? 255 : h * 8);
        uint16_t c = lerp565(cfg::COL_PURPLE, cfg::COL_CYAN, t);
        s_canvas.fillRect(x, sy - h, bw, h, c);
    }

    if (st.paused && st.active) {
        s_canvas.setFont(&fonts::efontJA_16);
        s_canvas.setTextDatum(textdatum_t::middle_center);
        s_canvas.setTextColor(cfg::COL_PURPLE);
        s_canvas.drawString("PAUSA", 120, 101);
        s_canvas.setTextDatum(textdatum_t::top_left);
    }

    char badge[56];
    if (st.active && st.codec.length()) {
        snprintf(badge, sizeof(badge), "%s  %.1fkHz  %lukbps",
                 st.codec.c_str(), st.sampleRate / 1000.0,
                 (unsigned long)(st.bitrate / 1000));
    } else if (st.active) {
        snprintf(badge, sizeof(badge), "lendo...");
    } else {
        badge[0] = 0;
    }
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(cfg::COL_DIM);
    s_canvas.drawString(badge, 4, 124);
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.drawString(st.active ? "SPC pausa  B lista" : "B lista", 236, 124);
    s_canvas.setTextDatum(textdatum_t::top_left);

    if (millis() < s_volToastEnd) drawVolToast();
}

void drawBrowser() {
    const BrowserState& bs = browser::state();
    s_canvas.fillSprite(cfg::COL_BG);
    drawHeader();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(cfg::COL_CYAN);
    String pvis = fitLeft(bs.path, 232);
    s_canvas.drawString(pvis.c_str(), 4, 16);

    const int rowH = 12;
    const int listY = 28;
    for (int r = 0; r < cfg::BROWSER_ROWS; r++) {
        int i = bs.top + r;
        if (i >= (int)bs.entries.size()) break;
        int y = listY + r * rowH;
        bool sel = (i == bs.sel);
        if (sel) s_canvas.fillRoundRect(2, y - 1, 236, rowH, 3, cfg::COL_SEL);
        s_canvas.setTextColor(sel ? cfg::COL_TEXT
                                  : (bs.entries[i].dir ? cfg::COL_DIR : cfg::COL_DIM));
        String label = (bs.entries[i].dir ? String("> ") : String("  ")) + bs.entries[i].name;
        String vis = truncRight(label, 230);
        s_canvas.drawString(vis.c_str(), 6, y + 1);
    }

    if ((int)bs.entries.size() > cfg::BROWSER_ROWS) {
        int trackY = listY;
        int trackH = cfg::BROWSER_ROWS * rowH;
        s_canvas.fillRect(239, trackY, 1, trackH, cfg::COL_TRACK);
        int n = (int)bs.entries.size();
        int thumbH = trackH * cfg::BROWSER_ROWS / n;
        if (thumbH < 6) thumbH = 6;
        int denom = n - cfg::BROWSER_ROWS;
        int thumbY = trackY + (denom > 0 ? (trackH - thumbH) * bs.top / denom : 0);
        s_canvas.fillRect(238, thumbY, 2, thumbH, cfg::COL_PURPLE);
    }

    s_canvas.setTextColor(cfg::COL_DIM);
    s_canvas.drawString("ENTER toca   DEL sobe   B fecha", 4, 124);

    if (millis() < s_volToastEnd) drawVolToast();
}

}

namespace ui {

void init() {
    s_canvas.setColorDepth(16);
    s_canvas.createSprite(240, 135);
    s_canvas.setTextWrap(false);
    for (int i = 0; i < SPEC_N; i++) s_spec[i] = 2 + (i % 8);
    s_invalid = true;
    s_lastDraw = 0;
}

void invalidate() { s_invalid = true; }

void toastVolume(uint8_t vol) {
    s_volToastVal = vol;
    s_volToastEnd = millis() + 900;
    s_invalid = true;
}

void tick() {
    uint32_t now = millis();
    const PlayerState& st = player::state();
    bool playing = st.active && st.playing && !st.paused;
    bool brows = browser::state().open;
    uint32_t interval = brows ? 150 : (playing ? 100 : 400);
    if (!s_invalid && now - s_lastDraw < interval) return;
    s_lastDraw = now;
    s_invalid = false;

    if (now >= s_batNext) {
        s_bat = M5.Power.getBatteryLevel();
        s_batNext = now + 5000;
    }

    if (playing) {
        if (now - s_lastSpec >= 100) {
            s_lastSpec = now;
            for (int i = 0; i < SPEC_N; i++) {
                s_seed = s_seed * 1664525u + 1013904223u;
                int delta = (int)((s_seed >> 24) & 0xFF) - 128;
                delta /= 24;
                int v = (int)s_spec[i] + delta;
                if (v < 2) v = 2;
                if (v > 30) v = 30;
                s_spec[i] = (uint8_t)v;
            }
        }
    } else {
        for (int i = 0; i < SPEC_N; i++) {
            if (s_spec[i] > 2) s_spec[i] -= 2;
            else s_spec[i] = 2;
        }
    }

    if (brows) drawBrowser();
    else drawNowPlaying();
    s_canvas.pushSprite(0, 0);
}

void showError(const char* msg) {
    s_canvas.fillSprite(cfg::COL_BG);
    s_canvas.setFont(&fonts::efontJA_16);
    s_canvas.setTextColor(cfg::COL_PURPLE);
    s_canvas.setTextDatum(textdatum_t::middle_center);
    s_canvas.drawString("CARDPLAYER", 120, 46);
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(cfg::COL_TEXT);
    s_canvas.drawString(msg, 120, 82);
    s_canvas.setTextColor(cfg::COL_DIM);
    s_canvas.drawString("Insira o cartao SD e reinicie", 120, 98);
    s_canvas.setTextDatum(textdatum_t::top_left);
    s_canvas.pushSprite(0, 0);
}

}
