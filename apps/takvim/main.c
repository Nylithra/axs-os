/* Takvim - AxsOS: ay görünümü, resmî tatiller, gün notları (~/.takvim-notlar) */
#include "axsapp.h"

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *AYLAR[12] = { "Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran",
                                 "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım", "Aralık" };
static const char *KISA[7] = { "Pzt", "Sal", "Çar", "Per", "Cum", "Cmt", "Paz" };
static const char *GUNLER[7] = { "Pazartesi", "Salı", "Çarşamba", "Perşembe", "Cuma", "Cumartesi", "Pazar" };

/* Sabit tarihli resmî tatiller (dini bayramlar ay takvimine göre değiştiği için yok) */
static const struct { int m, d; const char *name; } TATIL[] = {
    { 1, 1, "Yılbaşı" },
    { 4, 23, "Ulusal Egemenlik ve Çocuk Bayramı" },
    { 5, 1, "Emek ve Dayanışma Günü" },
    { 5, 19, "Atatürk'ü Anma, Gençlik ve Spor Bayramı" },
    { 7, 15, "Demokrasi ve Millî Birlik Günü" },
    { 8, 30, "Zafer Bayramı" },
    { 10, 29, "Cumhuriyet Bayramı" },
};

static int year, month; /* month 0..11 */
static int sel_d;       /* seçili gün */
static int ty, tmo, td; /* bugün */

#define MAXN 256
static struct { int y, m, d; char text[120]; } notes[MAXN];
static int nnotes;
static TextField tf;
static int editing;

static const char *notes_path(void)
{
    static char p[256];
    snprintf(p, sizeof p, "%s/.takvim-notlar", getenv("HOME") ? getenv("HOME") : "/root");
    return p;
}

static void notes_load(void)
{
    FILE *f = fopen(notes_path(), "r");
    if (!f)
        return;
    char line[200];
    while (nnotes < MAXN && fgets(line, sizeof line, f)) {
        int y, m, d, n = 0;
        if (sscanf(line, "%d-%d-%d %n", &y, &m, &d, &n) == 3 && n) {
            line[strcspn(line, "\n")] = 0;
            notes[nnotes].y = y, notes[nnotes].m = m, notes[nnotes].d = d;
            snprintf(notes[nnotes].text, sizeof notes[0].text, "%s", line + n);
            nnotes++;
        }
    }
    fclose(f);
}

static void notes_save(void)
{
    FILE *f = fopen(notes_path(), "w");
    if (!f)
        return;
    for (int i = 0; i < nnotes; i++)
        fprintf(f, "%04d-%02d-%02d %s\n", notes[i].y, notes[i].m, notes[i].d, notes[i].text);
    fclose(f);
}

static int note_find(int y, int m, int d)
{
    for (int i = 0; i < nnotes; i++)
        if (notes[i].y == y && notes[i].m == m && notes[i].d == d)
            return i;
    return -1;
}

static void note_set(int y, int m, int d, const char *text)
{
    int i = note_find(y, m, d);
    if (!text[0]) {
        if (i >= 0)
            notes[i] = notes[--nnotes];
    } else {
        if (i < 0) {
            if (nnotes >= MAXN)
                return;
            i = nnotes++;
        }
        notes[i].y = y, notes[i].m = m, notes[i].d = d;
        snprintf(notes[i].text, sizeof notes[i].text, "%s", text);
    }
    notes_save();
}

static const char *holiday(int m, int d)
{
    for (size_t i = 0; i < sizeof TATIL / sizeof TATIL[0]; i++)
        if (TATIL[i].m == m && TATIL[i].d == d)
            return TATIL[i].name;
    return NULL;
}

static int days_in(int y, int m)
{
    static const int D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 1 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : D[m];
}

/* 0 = Pazartesi */
static int weekday(int y, int m, int d)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int yy = y - (m < 2);
    int w = (yy + yy / 4 - yy / 100 + yy / 400 + t[m] + d) % 7; /* 0 = Pazar */
    return (w + 6) % 7;
}

static void go_month(int delta)
{
    month += delta;
    while (month < 0) month += 12, year--;
    while (month > 11) month -= 12, year++;
    sel_d = clampi(sel_d, 1, days_in(year, month));
    editing = 0;
}

static void start_edit(void)
{
    int i = note_find(year, month + 1, sel_d);
    tf_set(&tf, i >= 0 ? notes[i].text : "");
    editing = 1;
}

static void draw(Surf *s, UiState *u)
{
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, T.base);
    int side = 270;
    int gw = W - side - 24;
    /* başlık */
    char t[200];
    snprintf(t, sizeof t, "%s %d", AYLAR[month], year);
    draw_text(s, F_UI_BOLD, 26, 22, 18, T.text, t);
    if (ui_button(s, u, (Rect){ gw - 150, 18, 40, 34 }, "‹", BTN_NORMAL))
        go_month(-1);
    if (ui_button(s, u, (Rect){ gw - 106, 18, 64, 34 }, "Bugün", BTN_NORMAL)) {
        year = ty, month = tmo, sel_d = td;
        editing = 0;
    }
    if (ui_button(s, u, (Rect){ gw - 38, 18, 40, 34 }, "›", BTN_NORMAL))
        go_month(1);
    /* gün adları */
    int gx = 16, gy = 70, cw = (gw - gx) / 7;
    for (int i = 0; i < 7; i++)
        draw_text_center(s, F_UI_BOLD, 12, (Rect){ gx + i * cw, gy, cw, 20 }, i >= 5 ? T.accent2 : T.muted, KISA[i]);
    gy += 26;
    int ch = (H - gy - 14) / 6;
    int first = weekday(year, month, 1), nd = days_in(year, month);
    int pm = month ? month - 1 : 11, py = month ? year : year - 1, pnd = days_in(py, pm);
    for (int cell = 0; cell < 42; cell++) {
        int col = cell % 7, row = cell / 7;
        int d = cell - first + 1;
        Rect c = { gx + col * cw + 2, gy + row * ch + 2, cw - 4, ch - 4 };
        if (d < 1 || d > nd) {
            char n[4];
            snprintf(n, sizeof n, "%d", d < 1 ? pnd + d : d - nd);
            draw_text(s, F_UI, 13, c.x + 8, c.y + 6, ALPHA(T.muted, 110), n);
            continue;
        }
        int today = year == ty && month == tmo && d == td;
        int sel = d == sel_d;
        const char *hol = holiday(month + 1, d);
        int hov = ui_hover(u, c);
        uint32_t bg = sel ? ALPHA(T.accent, 70) : hov ? T.surface2 : T.surface;
        fill_rrect(s, c, 10, bg);
        if (sel)
            stroke_rrect(s, c, 10, 2, T.accent);
        char n[4];
        snprintf(n, sizeof n, "%d", d);
        if (today) {
            fill_circle(s, c.x + 18.f, c.y + 18.f, 13, T.accent);
            draw_text_center(s, F_UI_BOLD, 13, (Rect){ c.x + 5, c.y + 5, 26, 26 }, HEX(0xFFFFFF), n);
        } else {
            draw_text(s, F_UI_BOLD, 14, c.x + 10, c.y + 8, hol ? T.red : col >= 5 ? T.accent2 : T.text, n);
        }
        if (hol && ch > 50)
            draw_text_fit(s, F_UI, 10, c.x + 8, c.y + c.h - 18, c.w - 12, T.red, hol);
        if (note_find(year, month + 1, d) >= 0)
            fill_circle(s, c.x + c.w - 12.f, c.y + 14.f, 4, T.yellow);
        if (ui_clicked(u, c)) {
            if (sel && u->clicks >= 2)
                start_edit();
            else {
                sel_d = d;
                editing = 0;
            }
        }
    }
    /* yan panel: seçili gün */
    Rect p = { W - side - 8, 16, side - 8, H - 32 };
    fill_rrect(s, p, 16, T.surface);
    int wd = weekday(year, month, sel_d);
    snprintf(t, sizeof t, "%d", sel_d);
    draw_text(s, F_UI_BOLD, 56, p.x + 20, p.y + 14, T.accent, t);
    snprintf(t, sizeof t, "%s %d", AYLAR[month], year);
    draw_text(s, F_UI_BOLD, 16, p.x + 20, p.y + 86, T.text, t);
    draw_text(s, F_UI, 14, p.x + 20, p.y + 110, T.subtext, GUNLER[wd]);
    int y = p.y + 144;
    const char *hol = holiday(month + 1, sel_d);
    if (hol) {
        fill_rrect(s, (Rect){ p.x + 14, y, p.w - 28, 54 }, 10, ALPHA(T.red, 40));
        draw_text(s, F_UI_BOLD, 11, p.x + 24, y + 8, T.red, "RESMÎ TATİL");
        draw_text_wrap(s, F_UI, 12, p.x + 24, y + 24, p.w - 48, 2, 15, T.text, hol);
        y += 66;
    }
    /* bugüne uzaklık */
    {
        struct tm a = { .tm_year = year - 1900, .tm_mon = month, .tm_mday = sel_d, .tm_hour = 12 };
        struct tm b = { .tm_year = ty - 1900, .tm_mon = tmo, .tm_mday = td, .tm_hour = 12 };
        long diff = (long)((timegm(&a) - timegm(&b)) / 86400);
        if (diff == 0) snprintf(t, sizeof t, "Bugün");
        else if (diff == 1) snprintf(t, sizeof t, "Yarın");
        else if (diff == -1) snprintf(t, sizeof t, "Dün");
        else if (diff > 0) snprintf(t, sizeof t, "%ld gün sonra", diff);
        else snprintf(t, sizeof t, "%ld gün önce", -diff);
        draw_text(s, F_UI, 13, p.x + 20, y, T.muted, t);
        y += 30;
    }
    draw_text(s, F_UI_BOLD, 12, p.x + 20, y, T.subtext, "NOT");
    y += 22;
    int ni = note_find(year, month + 1, sel_d);
    if (editing) {
        Rect fr = { p.x + 14, y, p.w - 28, 36 };
        tf_draw(s, &tf, fr, "Not yaz, Enter ile kaydet");
        y += 46;
        if (ui_button(s, u, (Rect){ p.x + 14, y, 110, 34 }, "Kaydet", BTN_PRIMARY)) {
            note_set(year, month + 1, sel_d, tf.buf);
            editing = 0;
        }
        if (ui_button(s, u, (Rect){ p.x + 130, y, 100, 34 }, "Vazgeç", BTN_GHOST))
            editing = 0;
    } else {
        if (ni >= 0) {
            draw_text_wrap(s, F_UI, 14, p.x + 20, y, p.w - 40, 6, 19, T.text, notes[ni].text);
            y += 19 * mini(6, 1 + text_width(F_UI, 14, notes[ni].text) / (p.w - 40)) + 14;
        } else {
            draw_text(s, F_UI, 13, p.x + 20, y, T.muted, "Bu gün için not yok");
            y += 30;
        }
        if (ui_button(s, u, (Rect){ p.x + 14, y, 120, 34 }, ni >= 0 ? "Düzenle" : "Not ekle", BTN_NORMAL))
            start_edit();
        if (ni >= 0 && ui_button(s, u, (Rect){ p.x + 140, y, 90, 34 }, "Sil", BTN_DANGER))
            note_set(year, month + 1, sel_d, "");
    }
    draw_text_wrap(s, F_UI, 11, p.x + 20, p.y + p.h - 44, p.w - 40, 2, 15, T.muted,
                   "PageUp/PageDown: ay • oklar: gün • Enter: not");
}

static void key(KeyEv *e)
{
    if (!e->down)
        return;
    if (editing) {
        int r = tf_key(&tf, e);
        if (r == 2) {
            note_set(year, month + 1, sel_d, tf.buf);
            editing = 0;
        } else if (e->code == KEY_ESC) {
            editing = 0;
        }
        return;
    }
    int nd = days_in(year, month);
    switch (e->code) {
    case KEY_PAGEUP: go_month(-1); break;
    case KEY_PAGEDOWN: go_month(1); break;
    case KEY_LEFT: sel_d > 1 ? sel_d-- : (go_month(-1), sel_d = days_in(year, month)); break;
    case KEY_RIGHT: sel_d < nd ? sel_d++ : (go_month(1), sel_d = 1); break;
    case KEY_UP: sel_d > 7 ? (sel_d -= 7) : 0; break;
    case KEY_DOWN: sel_d + 7 <= nd ? (sel_d += 7) : 0; break;
    case KEY_ENTER: case KEY_KPENTER: start_edit(); break;
    case KEY_T: year = ty, month = tmo, sel_d = td; break;
    }
}

int main(void)
{
    config_load(); /* saat dilimi (axsapp_open da yükler; bugünü hesaplamak için şimdi gerekli) */
    time_t now = time(NULL) + tz_offset_min * 60;
    struct tm tm;
    gmtime_r(&now, &tm);
    ty = year = tm.tm_year + 1900;
    tmo = month = tm.tm_mon;
    td = sel_d = tm.tm_mday;
    notes_load();
    AxsAppFuncs f = { .draw = draw, .key = key };
    return axsapp_run("takvim", "Takvim", 900, 600, &f);
}
