/* Saat - AxsOS: analog/dijital saat, kronometre, zamanlayıcı */
#include "axsapp.h"

#include <linux/input.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int tab; /* 0 saat, 1 kronometre, 2 zamanlayıcı */
static const char *TABS[3] = { "Saat", "Kronometre", "Zamanlayıcı" };
static const char *GUNLER[7] = { "Pazar", "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi" };
static const char *AYLAR[12] = { "Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
                                 "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık" };

/* kronometre */
static int sw_run;
static long sw_acc, sw_t0; /* ms */
static long laps[64];
static int nlaps;
/* zamanlayıcı */
static int tm_set = 5 * 60, tm_run, tm_done;
static long tm_end, tm_left;

static struct tm local_now(void)
{
    time_t t = time(NULL) + tz_offset_min * 60;
    struct tm tm;
    gmtime_r(&t, &tm);
    return tm;
}

static long sw_value(void) { return sw_acc + (sw_run ? now_ms() - sw_t0 : 0); }

static void fmt_ms(char *out, size_t n, long ms)
{
    long cs = ms / 10;
    snprintf(out, n, "%02ld:%02ld,%02ld", cs / 6000, cs / 100 % 60, cs % 100);
}

static void draw_clock(Surf *s, int cx, int cy, int r)
{
    struct tm tm = local_now();
    float sec = tm.tm_sec, min = tm.tm_min + sec / 60, hour = tm.tm_hour % 12 + min / 60;
    draw_shadow(s, (Rect){ cx - r, cy - r, 2 * r, 2 * r }, r, 24, 8, 90);
    fill_circle(s, cx, cy, r, T.surface2);
    fill_circle(s, cx, cy, r - 6, T.surface);
    stroke_circle(s, cx, cy, r, 2, ALPHA(T.accent, 160));
    for (int i = 0; i < 60; i++) {
        float a = i * 6.2831853f / 60;
        float r0 = i % 5 ? r - 16 : r - 24, r1 = r - 12;
        draw_line(s, cx + r0 * sinf(a), cy - r0 * cosf(a), cx + r1 * sinf(a), cy - r1 * cosf(a),
                  i % 5 ? 1.2f : 3, i % 5 ? T.muted : T.text);
    }
    for (int i = 1; i <= 12; i++) {
        float a = i * 6.2831853f / 12, rr = r - 44;
        char t[4];
        snprintf(t, sizeof t, "%d", i);
        draw_text_center(s, F_UI_BOLD, 18, (Rect){ (int)(cx + rr * sinf(a)) - 20, (int)(cy - rr * cosf(a)) - 12, 40, 24 },
                         T.subtext, t);
    }
    float ha = hour * 6.2831853f / 12, ma = min * 6.2831853f / 60, sa = sec * 6.2831853f / 60;
    draw_line(s, cx, cy, cx + (r * 0.5f) * sinf(ha), cy - (r * 0.5f) * cosf(ha), 7, T.text);
    draw_line(s, cx, cy, cx + (r * 0.74f) * sinf(ma), cy - (r * 0.74f) * cosf(ma), 4.5f, T.text);
    draw_line(s, cx - 18 * sinf(sa), cy + 18 * cosf(sa), cx + (r * 0.82f) * sinf(sa), cy - (r * 0.82f) * cosf(sa), 2,
              T.accent);
    fill_circle(s, cx, cy, 7, T.accent);
    fill_circle(s, cx, cy, 3, T.surface);
}

static void draw(Surf *s, UiState *u)
{
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, T.base);
    /* sekmeler */
    int tw = 130, tx = (W - 3 * tw) / 2;
    fill_rrect(s, (Rect){ tx - 4, 12, 3 * tw + 8, 42 }, 12, T.surface);
    for (int i = 0; i < 3; i++)
        if (ui_button(s, u, (Rect){ tx + i * tw, 16, tw, 34 }, TABS[i], i == tab ? BTN_PRIMARY : BTN_GHOST))
            tab = i;
    char t[96];
    if (tab == 0) {
        int r = mini(W, H - 200) / 2 - 20;
        draw_clock(s, W / 2, 72 + r + 10, r);
        struct tm tm = local_now();
        snprintf(t, sizeof t, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        draw_text_center(s, F_MONO_BOLD, 34, (Rect){ 0, H - 110, W, 44 }, T.text, t);
        snprintf(t, sizeof t, "%d %s %d, %s", tm.tm_mday, AYLAR[tm.tm_mon], tm.tm_year + 1900, GUNLER[tm.tm_wday]);
        draw_text_center(s, F_UI, 16, (Rect){ 0, H - 62, W, 24 }, T.subtext, t);
        int off = tz_offset_min;
        snprintf(t, sizeof t, "Saat dilimi: UTC%c%d%s", off < 0 ? '-' : '+', abs(off) / 60, off % 60 ? ":30" : "");
        draw_text_center(s, F_UI, 12, (Rect){ 0, H - 36, W, 18 }, T.muted, t);
    } else if (tab == 1) {
        long v = sw_value();
        fmt_ms(t, sizeof t, v);
        Rect disp = { 30, 80, W - 60, 120 };
        fill_rrect_vgrad(s, disp, 20, T.surface2, T.surface);
        draw_text_center(s, F_MONO_BOLD, 54, disp, sw_run ? T.text : T.subtext, t);
        int bx = W / 2 - 150;
        if (ui_button(s, u, (Rect){ bx, 220, 140, 44 }, sw_run ? "Durdur" : sw_acc ? "Devam" : "Başlat",
                      sw_run ? BTN_DANGER : BTN_PRIMARY)) {
            if (sw_run)
                sw_acc += now_ms() - sw_t0;
            else
                sw_t0 = now_ms();
            sw_run = !sw_run;
        }
        if (ui_button(s, u, (Rect){ bx + 160, 220, 140, 44 }, sw_run ? "Tur" : "Sıfırla", BTN_NORMAL)) {
            if (sw_run) {
                if (nlaps < 64)
                    laps[nlaps++] = v;
            } else {
                sw_acc = 0;
                nlaps = 0;
            }
        }
        int y = 286;
        for (int i = nlaps - 1; i >= 0 && y < H - 30; i--, y += 34) {
            Rect row = { 40, y, W - 80, 30 };
            fill_rrect(s, row, 8, T.surface);
            char a[32], b[32], l[24];
            fmt_ms(a, sizeof a, laps[i]);
            fmt_ms(b, sizeof b, laps[i] - (i ? laps[i - 1] : 0));
            snprintf(l, sizeof l, "Tur %d", i + 1);
            draw_text(s, F_UI_BOLD, 14, row.x + 14, row.y + 7, T.subtext, l);
            draw_text(s, F_MONO, 14, row.x + row.w / 2 - 40, row.y + 7, T.muted, b);
            draw_text(s, F_MONO_BOLD, 14, row.x + row.w - 14 - text_width(F_MONO_BOLD, 14, a), row.y + 7, T.text, a);
        }
    } else {
        long left = tm_run ? tm_end - now_ms() : tm_left ? tm_left : tm_set * 1000L;
        if (tm_run && left <= 0) {
            tm_run = 0;
            tm_left = 0;
            tm_done = 1;
            left = 0;
        }
        long sec = (left + 999) / 1000;
        snprintf(t, sizeof t, "%02ld:%02ld:%02ld", sec / 3600, sec / 60 % 60, sec % 60);
        int cx = W / 2, cy = 210, r = 120;
        float frac = tm_run || tm_left ? (float)left / (tm_set * 1000.f) : 1;
        stroke_circle(s, cx, cy, r, 10, T.surface2);
        /* ilerleme yayı */
        int n = (int)(120 * frac);
        for (int i = 0; i < n; i++) {
            float a0 = i * 6.2831853f / 120, a1 = (i + 1) * 6.2831853f / 120;
            draw_line(s, cx + r * sinf(a0), cy - r * cosf(a0), cx + r * sinf(a1), cy - r * cosf(a1), 10,
                      tm_done ? T.red : T.accent);
        }
        draw_text_center(s, F_MONO_BOLD, 40, (Rect){ cx - r, cy - 30, 2 * r, 50 }, tm_done ? T.red : T.text, t);
        if (tm_done)
            draw_text_center(s, F_UI_BOLD, 15, (Rect){ cx - r, cy + 24, 2 * r, 22 }, T.red, "Süre doldu!");
        if (!tm_run && !tm_left) {
            static const struct { const char *l; int d; } ADJ[6] = {
                { "−1 dk", -60 }, { "+1 dk", 60 }, { "+5 dk", 300 }, { "−10 sn", -10 }, { "+10 sn", 10 }, { "+1 sa", 3600 },
            };
            for (int i = 0; i < 6; i++) {
                Rect b = { W / 2 - 207 + i * 70, 352, 64, 34 };
                if (ui_button(s, u, b, ADJ[i].l, BTN_NORMAL)) {
                    tm_set = clampi(tm_set + ADJ[i].d, 10, 24 * 3600 - 1);
                    tm_done = 0;
                }
            }
        }
        int by = mini(404, H - 60);
        if (ui_button(s, u, (Rect){ W / 2 - 150, by, 140, 44 }, tm_run ? "Duraklat" : tm_left ? "Devam" : "Başlat",
                      tm_run ? BTN_DANGER : BTN_PRIMARY)) {
            if (tm_run) {
                tm_left = tm_end - now_ms();
                tm_run = 0;
            } else {
                tm_end = now_ms() + (tm_left ? tm_left : tm_set * 1000L);
                tm_left = 0;
                tm_run = 1;
                tm_done = 0;
            }
        }
        if (ui_button(s, u, (Rect){ W / 2 + 10, by, 140, 44 }, "Sıfırla", BTN_NORMAL)) {
            tm_run = 0;
            tm_left = 0;
            tm_done = 0;
        }
    }
}

static void tick(void)
{
    static int last_sec = -1;
    if (tab == 1 && sw_run) {
        axsapp_redraw();
        return;
    }
    struct tm tm = local_now();
    if (tm.tm_sec != last_sec) {
        last_sec = tm.tm_sec;
        axsapp_redraw();
    }
}

static void key(KeyEv *e)
{
    if (!e->down)
        return;
    if (e->code == KEY_TAB)
        tab = (tab + 1) % 3;
    else if (e->code >= KEY_1 && e->code <= KEY_3)
        tab = e->code - KEY_1;
}

int main(void)
{
    AxsAppFuncs f = { .draw = draw, .key = key, .timer = tick, .interval_ms = 100 };
    return axsapp_run("saat", "Saat", 480, 560, &f);
}
