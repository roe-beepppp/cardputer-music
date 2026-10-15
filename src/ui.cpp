#include "ui.h"
#include "config.h"
#include "player.h"
#include "browser.h"
#include <M5Unified.h>

namespace {

M5Canvas s_canvas(&M5.Display);

struct Theme {
    const char* name;
    uint16_t bg, primary, accent, text, dim, track, sel, dir;
};

const Theme s_themes[3] = {
    { "Terminal",    0x0000, 0x37EC, 0x9FF3, 0xCFF9, 0x1BC6, 0x0922, 0x11E3, 0x9FF3 },
    { "Now Playing", 0x0842, 0xAC1F, 0x5EFD, 0xFFFF, 0x8C53, 0x2106, 0x38AB, 0xC9B4 },
    { "Tape Deck",   0x1040, 0xFD80, 0xFED0, 0xFF58, 0x8B02, 0x3101, 0x4981, 0xFED0 },
};
uint8_t s_themeIdx = 1;
const Theme& TH() { return s_themes[s_themeIdx]; }

uint32_t s_lastDraw = 0;
uint32_t s_lastSpec = 0;
bool s_invalid = true;

uint32_t s_volToastEnd = 0;
uint8_t s_volToastVal = 0;
uint32_t s_msgToastEnd = 0;
char s_msgToast[28] = "";

int s_marqueeX = 0;
uint32_t s_marqueeLast = 0;

constexpr int SPEC_N = 28;
uint8_t s_spec[SPEC_N];
uint32_t s_seed = 0x12345678;

int s_bat = -1;
uint32_t s_batNext = 0;

uint8_t s_reelStep = 0;

const int8_t s_cos12[12] = { 100, 87, 50, 0, -50, -87, -100, -87, -50, 0, 50, 87 };
const int8_t s_sin12[12] = { 0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50 };

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
    s_canvas.drawRect(x, y, 16, 9, TH().dim);
    s_canvas.fillRect(x + 16, y + 2, 2, 5, TH().dim);
    char buf[8];
    if (s_bat >= 0) {
        int w = (14 * s_bat) / 100;
        if (w > 0) {
            s_canvas.fillRect(x + 1, y + 1, w, 7, s_bat > 25 ? TH().accent : TH().primary);
        }
        snprintf(buf, sizeof(buf), "%d%%", s_bat);
    } else {
        snprintf(buf, sizeof(buf), "--%%");
    }
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString(buf, 240 - 4, 2);
    s_canvas.setTextDatum(textdatum_t::top_left);
}

void drawHeader() {
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextDatum(textdatum_t::top_left);
    s_canvas.setTextColor(TH().primary);
    s_canvas.drawString("CARDPLAYER", 4, 2);
    drawBattery();
}

void drawVolToast() {
    int x = 78, y = 52, w = 84, h = 28;
    s_canvas.fillRoundRect(x, y, w, h, 4, TH().track);
    s_canvas.drawRoundRect(x, y, w, h, 4, TH().primary);
    char buf[16];
    snprintf(buf, sizeof(buf), "VOL %u", (unsigned)s_volToastVal);
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().text);
    s_canvas.setTextDatum(textdatum_t::middle_center);
    s_canvas.drawString(buf, x + w / 2, y + 9);
    s_canvas.drawRect(x + 8, y + 19, w - 16, 4, TH().dim);
    if (s_volToastVal > 0) {
        int fw = (w - 18) * s_volToastVal / cfg::VOLUME_MAX;
        if (fw > 0) s_canvas.fillRect(x + 9, y + 20, fw, 2, TH().accent);
    }
    s_canvas.setTextDatum(textdatum_t::middle_center);
}

void drawMsgToast() {
    int w = 150, h = 26, x = (240 - w) / 2, y = 52;
    s_canvas.fillRoundRect(x, y, w, h, 4, TH().track);
    s_canvas.drawRoundRect(x, y, w, h, 4, TH().primary);
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().text);
    s_canvas.setTextDatum(textdatum_t::middle_center);
    s_canvas.drawString(s_msgToast, x + w / 2, y + h / 2 + 1);
    s_canvas.setTextDatum(textdatum_t::top_left);
}

int marqueeScroll(const String& title) {
    int tw = s_canvas.textWidth(title.c_str());
    if (tw <= 232) {
        s_marqueeX = 0;
        return 0;
    }
    uint32_t now = millis();
    if (now - s_marqueeLast >= 40) {
        s_marqueeX += 2;
        s_marqueeLast = now;
    }
    int span = tw + 60;
    if (s_marqueeX > span) s_marqueeX = 0;
    return s_marqueeX;
}

float trackFrac(const PlayerState& st) {
    if (!st.duration) return 0;
    float f = (float)st.position / (float)st.duration;
    if (f > 1) f = 1;
    return f;
}

void drawSpectrum(int sx, int sy, int bw, int gap) {
    for (int i = 0; i < SPEC_N; i++) {
        int h = s_spec[i];
        if (h < 2) h = 2;
        int x = sx + i * (bw + gap);
        uint8_t t = (uint8_t)(h * 8 > 255 ? 255 : h * 8);
        uint16_t c = lerp565(TH().primary, TH().accent, t);
        s_canvas.fillRect(x, sy - h, bw, h, c);
    }
}

void drawNowPlayingA() {
    const PlayerState& st = player::state();
    s_canvas.fillSprite(TH().bg);
    drawHeader();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().dim);
    if (st.active) {
        int i = st.path.lastIndexOf('/');
        String folder = i > 0 ? st.path.substring(0, i) : String("/sd");
        String vis = fitLeft("> " + folder, 232);
        s_canvas.drawString(vis.c_str(), 4, 16);
    } else {
        s_canvas.drawString("> nenhuma musica - B abre a lista", 4, 16);
    }

    s_canvas.setFont(&fonts::efontJA_16);
    String title = st.active ? st.title : String("CARDPLAYER");
    int scroll = marqueeScroll(title);
    s_canvas.setTextColor(TH().text);
    s_canvas.setClipRect(4, 28, 232, 22);
    s_canvas.drawString(title.c_str(), 4 - scroll, 29);
    s_canvas.clearClipRect();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString(st.artist.c_str(), 4, 52);
    if (st.count > 0) {
        char idx[24];
        snprintf(idx, sizeof(idx), "%d/%d", st.index + 1, st.count);
        s_canvas.setTextDatum(textdatum_t::top_right);
        s_canvas.setTextColor(TH().primary);
        s_canvas.drawString(idx, 236, 52);
        s_canvas.setTextDatum(textdatum_t::top_left);
    }

    float frac = trackFrac(st);
    char bar[32];
    int k = (int)(24 * frac);
    bar[0] = '[';
    for (int i = 0; i < 24; i++) bar[1 + i] = (i < k) ? '#' : '.';
    bar[25] = ']';
    bar[26] = 0;
    s_canvas.setTextColor(TH().primary);
    s_canvas.drawString(bar, 8, 66);
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)(frac * 100));
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.drawString(pct, 232, 66);
    s_canvas.setTextDatum(textdatum_t::top_left);

    char cur[12], dur[12];
    fmtTime(cur, sizeof(cur), st.position);
    fmtTime(dur, sizeof(dur), st.duration);
    String line = String(cur) + " / " + String(dur);
    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString(line.c_str(), 8, 80);

    drawSpectrum(8, 120, 4, 4);

    if (st.paused && st.active) {
        s_canvas.setFont(&fonts::efontJA_16);
        s_canvas.setTextDatum(textdatum_t::middle_center);
        s_canvas.setTextColor(TH().primary);
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
    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString(badge, 4, 124);
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.drawString("SPC  B  V", 236, 124);
    s_canvas.setTextDatum(textdatum_t::top_left);

    if (millis() < s_volToastEnd) drawVolToast();
    else if (millis() < s_msgToastEnd) drawMsgToast();
}

void drawNowPlayingB() {
    const PlayerState& st = player::state();
    s_canvas.fillSprite(TH().bg);
    drawHeader();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().dim);
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
    int scroll = marqueeScroll(title);
    s_canvas.setTextColor(TH().text);
    s_canvas.setClipRect(4, 28, 232, 22);
    s_canvas.drawString(title.c_str(), 4 - scroll, 29);
    s_canvas.clearClipRect();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().accent);
    s_canvas.drawString(st.artist.c_str(), 4, 52);
    if (st.count > 0) {
        char idx[24];
        snprintf(idx, sizeof(idx), "%d/%d", st.index + 1, st.count);
        s_canvas.setTextDatum(textdatum_t::top_right);
        s_canvas.setTextColor(TH().primary);
        s_canvas.drawString(idx, 236, 52);
        s_canvas.setTextDatum(textdatum_t::top_left);
    }

    int px = 8, py = 66, pw = 224, ph = 8;
    s_canvas.fillRoundRect(px, py, pw, ph, 4, TH().track);
    float frac = trackFrac(st);
    int fw = (int)(pw * frac);
    if (fw > 0) {
        s_canvas.fillRoundRect(px, py, fw, ph, 4, TH().primary);
        int kx = px + fw;
        if (kx > px + pw - 4) kx = px + pw - 4;
        if (kx < px + 4) kx = px + 4;
        s_canvas.fillCircle(kx, py + ph / 2, 5, TH().accent);
    }

    char cur[12], rem[12];
    fmtTime(cur, sizeof(cur), st.position);
    uint32_t rd = st.duration > st.position ? st.duration - st.position : 0;
    fmtTime(rem, sizeof(rem), rd);
    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString(cur, px, 78);
    String rems = "-" + String(rem);
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.drawString(rems.c_str(), px + pw, 78);
    s_canvas.setTextDatum(textdatum_t::top_left);

    drawSpectrum(8, 120, 4, 4);

    if (st.paused && st.active) {
        s_canvas.setFont(&fonts::efontJA_16);
        s_canvas.setTextDatum(textdatum_t::middle_center);
        s_canvas.setTextColor(TH().primary);
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
    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString(badge, 4, 124);
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.drawString("SPC  B  V", 236, 124);
    s_canvas.setTextDatum(textdatum_t::top_left);

    if (millis() < s_volToastEnd) drawVolToast();
    else if (millis() < s_msgToastEnd) drawMsgToast();
}

void drawReel(int cx, int cy) {
    s_canvas.drawCircle(cx, cy, 13, TH().primary);
    for (int k = 0; k < 3; k++) {
        int idx = (s_reelStep + k * 4) % 12;
        int x2 = cx + (s_cos12[idx] * 11) / 100;
        int y2 = cy + (s_sin12[idx] * 11) / 100;
        s_canvas.drawLine(cx, cy, x2, y2, TH().accent);
    }
    s_canvas.fillCircle(cx, cy, 4, TH().primary);
}

void drawNowPlayingC() {
    const PlayerState& st = player::state();
    s_canvas.fillSprite(TH().bg);
    drawHeader();

    s_canvas.drawRoundRect(10, 24, 220, 64, 6, TH().primary);
    s_canvas.fillRoundRect(16, 28, 208, 26, 4, TH().track);

    s_canvas.setFont(&fonts::efontJA_16);
    String title = st.active ? st.title : String("CARDPLAYER");
    int scroll = marqueeScroll(title);
    s_canvas.setTextColor(TH().text);
    s_canvas.setClipRect(18, 29, 204, 19);
    s_canvas.drawString(title.c_str(), 20 - scroll, 30);
    s_canvas.clearClipRect();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().dim);
    s_canvas.setClipRect(18, 44, 204, 11);
    s_canvas.drawString(st.artist.c_str(), 20, 45);
    s_canvas.clearClipRect();

    s_canvas.drawRect(96, 58, 48, 26, TH().primary);
    float frac = trackFrac(st);
    s_canvas.fillRect(100, 68, 40, 6, TH().track);
    int wfw = (int)(40 * frac);
    if (wfw > 0) s_canvas.fillRect(100, 68, wfw, 6, TH().primary);

    drawReel(66, 71);
    drawReel(174, 71);

    char cur[12], rem[12];
    fmtTime(cur, sizeof(cur), st.position);
    uint32_t rd = st.duration > st.position ? st.duration - st.position : 0;
    fmtTime(rem, sizeof(rem), rd);
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().primary);
    s_canvas.drawString(cur, 14, 92);
    String rems = "-" + String(rem);
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.drawString(rems.c_str(), 226, 92);
    if (st.count > 0) {
        char idx[24];
        snprintf(idx, sizeof(idx), "%d/%d", st.index + 1, st.count);
        s_canvas.setTextDatum(textdatum_t::top_center);
        s_canvas.setTextColor(TH().dim);
        s_canvas.drawString(idx, 120, 92);
    }
    s_canvas.setTextDatum(textdatum_t::top_left);

    s_canvas.fillRoundRect(14, 104, 212, 5, 2, TH().track);
    int bfw = (int)(212 * frac);
    if (bfw > 0) s_canvas.fillRoundRect(14, 104, bfw, 5, 2, TH().primary);

    if (st.paused && st.active) {
        s_canvas.fillRoundRect(88, 57, 64, 26, 4, TH().track);
        s_canvas.drawRoundRect(88, 57, 64, 26, 4, TH().primary);
        s_canvas.setFont(&fonts::efontJA_16);
        s_canvas.setTextDatum(textdatum_t::middle_center);
        s_canvas.setTextColor(TH().primary);
        s_canvas.drawString("PAUSA", 120, 71);
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
    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString(badge, 4, 122);
    s_canvas.setTextDatum(textdatum_t::top_right);
    s_canvas.drawString("SPC  B  V", 236, 122);
    s_canvas.setTextDatum(textdatum_t::top_left);

    if (millis() < s_volToastEnd) drawVolToast();
    else if (millis() < s_msgToastEnd) drawMsgToast();
}

void drawBrowser() {
    const BrowserState& bs = browser::state();
    s_canvas.fillSprite(TH().bg);
    drawHeader();

    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().accent);
    String pvis = fitLeft(bs.path, 232);
    s_canvas.drawString(pvis.c_str(), 4, 16);

    const int rowH = 12;
    const int listY = 28;
    for (int r = 0; r < cfg::BROWSER_ROWS; r++) {
        int i = bs.top + r;
        if (i >= (int)bs.entries.size()) break;
        int y = listY + r * rowH;
        bool sel = (i == bs.sel);
        if (sel) s_canvas.fillRoundRect(2, y - 1, 236, rowH, 3, TH().sel);
        s_canvas.setTextColor(sel ? TH().text
                                  : (bs.entries[i].dir ? TH().dir : TH().dim));
        String label = (bs.entries[i].dir ? String("> ") : String("  ")) + bs.entries[i].name;
        String vis = truncRight(label, 230);
        s_canvas.drawString(vis.c_str(), 6, y + 1);
    }

    if ((int)bs.entries.size() > cfg::BROWSER_ROWS) {
        int trackY = listY;
        int trackH = cfg::BROWSER_ROWS * rowH;
        s_canvas.fillRect(239, trackY, 1, trackH, TH().track);
        int n = (int)bs.entries.size();
        int thumbH = trackH * cfg::BROWSER_ROWS / n;
        if (thumbH < 6) thumbH = 6;
        int denom = n - cfg::BROWSER_ROWS;
        int thumbY = trackY + (denom > 0 ? (trackH - thumbH) * bs.top / denom : 0);
        s_canvas.fillRect(238, thumbY, 2, thumbH, TH().primary);
    }

    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString("ENTER toca   DEL sobe   B fecha", 4, 124);

    if (millis() < s_volToastEnd) drawVolToast();
    else if (millis() < s_msgToastEnd) drawMsgToast();
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

void setTheme(uint8_t idx) {
    if (idx > 2) idx = 0;
    s_themeIdx = idx;
    s_invalid = true;
}

uint8_t theme() { return s_themeIdx; }

const char* themeName(uint8_t idx) {
    if (idx > 2) idx = 0;
    return s_themes[idx].name;
}

void toastVolume(uint8_t vol) {
    s_volToastVal = vol;
    s_volToastEnd = millis() + 900;
    s_invalid = true;
}

void toastMsg(const char* msg) {
    strlcpy(s_msgToast, msg, sizeof(s_msgToast));
    s_msgToastEnd = millis() + 1200;
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
            s_reelStep = (s_reelStep + 1) % 12;
        }
    } else {
        for (int i = 0; i < SPEC_N; i++) {
            if (s_spec[i] > 2) s_spec[i] -= 2;
            else s_spec[i] = 2;
        }
    }

    if (brows) drawBrowser();
    else if (s_themeIdx == 0) drawNowPlayingA();
    else if (s_themeIdx == 1) drawNowPlayingB();
    else drawNowPlayingC();
    s_canvas.pushSprite(0, 0);
}

void showError(const char* msg) {
    s_canvas.fillSprite(TH().bg);
    s_canvas.setFont(&fonts::efontJA_16);
    s_canvas.setTextColor(TH().primary);
    s_canvas.setTextDatum(textdatum_t::middle_center);
    s_canvas.drawString("CARDPLAYER", 120, 46);
    s_canvas.setFont(&fonts::efontJA_10);
    s_canvas.setTextColor(TH().text);
    s_canvas.drawString(msg, 120, 82);
    s_canvas.setTextColor(TH().dim);
    s_canvas.drawString("Insira o cartao SD e reinicie", 120, 98);
    s_canvas.setTextDatum(textdatum_t::top_left);
    s_canvas.pushSprite(0, 0);
}

}
