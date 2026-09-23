/* Mayın Tarlası - AxsOS oyunu */
#include "axsapp.h"

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXW 20
#define MAXH 16
#define CELL 30
#define TOP 70

static const struct { const char *name; int w, h, mines; } LEVELS[3] = {
    { "Kolay", 9, 9, 10 }, { "Orta", 14, 12, 28 }, { "Zor", 20, 16, 60 },
};
static int level = 1, GW, GH, NM;
static int mine[MAXH][MAXW], open_[MAXH][MAXW], flag[MAXH][MAXW], cnt[MAXH][MAXW];
static int started, dead, won, opened, flags;
static time_t t0;
static int elapsed;

static void setup(int lv)
{
    level = lv;
    GW = LEVELS[lv].w;
    GH = LEVELS[lv].h;
    NM = LEVELS[lv].mines;
    memset(mine, 0, sizeof mine);
    memset(open_, 0, sizeof open_);
    memset(flag, 0, sizeof flag);
    started = dead = won = opened = flags = elapsed = 0;
}

static int win_w(void) { return maxi(470, GW * CELL + 40); }
static int win_h(void) { return GH * CELL + TOP + 24; }

/* İlk tıklanan hücre ve komşuları mayınsız */
static void lay(int fx, int fy)
{
    int n = 0;
    while (n < NM) {
        int x = rand() % GW, y = rand() % GH;
        if (mine[y][x] || (abs(x - fx) <= 1 && abs(y - fy) <= 1))
            continue;
        mine[y][x] = 1;
        n++;
    }
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            int c = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < GW && ny < GH && mine[ny][nx])
                        c++;
                }
            cnt[y][x] = c;
        }
    started = 1;
    t0 = time(NULL);
}

static void reveal(int x, int y)
{
    if (x < 0 || y < 0 || x >= GW || y >= GH || open_[y][x] || flag[y][x])
        return;
    open_[y][x] = 1;
    opened++;
    if (mine[y][x]) {
        dead = 1;
        return;
    }
    if (!cnt[y][x])
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                reveal(x + dx, y + dy);
}

static void check_win(void)
{
    if (!dead && opened == GW * GH - NM) {
        won = 1;
        for (int y = 0; y < GH; y++)
            for (int x = 0; x < GW; x++)
                if (mine[y][x])
                    flag[y][x] = 1;
        flags = NM;
    }
}

static Rect field_rect(Surf *s) { return (Rect){ (s->w - GW * CELL) / 2, TOP, GW * CELL, GH * CELL }; }

static const uint32_t NUMC[9] = { 0, 0xFF60A5FA, 0xFF4ADE80, 0xFFF87171, 0xFFA78BFA,
                                  0xFFFB923C, 0xFF22D3EE, 0xFFE2E8F0, 0xFF94A3B8 };

static void draw_mine(Surf *s, float cx, float cy, float r)
{
    for (int i = 0; i < 4; i++) {
        float a = i * 3.14159f / 4;
        draw_line(s, cx - r * 1.4f * __builtin_cosf(a), cy - r * 1.4f * __builtin_sinf(a),
                  cx + r * 1.4f * __builtin_cosf(a), cy + r * 1.4f * __builtin_sinf(a), 2, HEX(0x111111));
    }
    fill_circle(s, cx, cy, r, HEX(0x111111));
    fill_circle(s, cx - r / 3, cy - r / 3, r / 3, ALPHA(HEX(0xFFFFFF), 180));
}

static void draw_flag(Surf *s, int x, int y)
{
    draw_line(s, x + 11.f, y + 7.f, x + 11.f, y + 23.f, 2, HEX(0xE2E8F0));
    fill_rect(s, (Rect){ x + 8, y + 22, 12, 2 }, HEX(0xE2E8F0));
    fill_rrect(s, (Rect){ x + 12, y + 7, 10, 8 }, 2, HEX(0xEF4444));
}

static void draw(Surf *s, UiState *u)
{
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, T.base);
    if (started && !dead && !won)
        elapsed = (int)(time(NULL) - t0);
    char t[48];
    snprintf(t, sizeof t, "Mayın %d", NM - flags);
    fill_rrect(s, (Rect){ 16, 14, 104, 40 }, 10, T.surface2);
    draw_text_center(s, F_UI_BOLD, 15, (Rect){ 16, 14, 104, 40 }, T.red, t);
    snprintf(t, sizeof t, "%02d:%02d", elapsed / 60, elapsed % 60);
    fill_rrect(s, (Rect){ W - 120, 14, 104, 40 }, 10, T.surface2);
    draw_text_center(s, F_MONO_BOLD, 17, (Rect){ W - 120, 14, 104, 40 }, T.text, t);
    /* zorluk düğmeleri */
    int bx = (W - 3 * 74 + 6) / 2;
    for (int i = 0; i < 3; i++) {
        Rect b = { bx + i * 74, 16, 68, 36 };
        if (ui_button(s, u, b, LEVELS[i].name, i == level ? BTN_PRIMARY : BTN_NORMAL)) {
            setup(i);
            axsapp_request_size(win_w(), win_h());
        }
    }
    Rect f = field_rect(s);
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) {
            Rect c = { f.x + x * CELL, f.y + y * CELL, CELL - 2, CELL - 2 };
            int hov = !dead && !won && !open_[y][x] && ui_hover(u, c);
            if (open_[y][x]) {
                fill_rrect(s, c, 5, mine[y][x] ? HEX(0x7F1D1D) : T.surface);
                if (mine[y][x])
                    draw_mine(s, c.x + c.w / 2.f, c.y + c.h / 2.f, 6);
                else if (cnt[y][x]) {
                    char n[2] = { (char)('0' + cnt[y][x]), 0 };
                    draw_text_center(s, F_UI_BOLD, 16, c, NUMC[cnt[y][x]], n);
                }
            } else {
                fill_rrect_vgrad(s, c, 5, hov ? lighten(T.overlay, 30) : T.overlay, lighten(T.overlay, -20));
                if (flag[y][x])
                    draw_flag(s, c.x, c.y);
                else if (dead && mine[y][x])
                    draw_mine(s, c.x + c.w / 2.f, c.y + c.h / 2.f, 6);
            }
            if (ui_clicked(u, c) && !dead && !won && !flag[y][x]) {
                if (!started)
                    lay(x, y);
                reveal(x, y);
                check_win();
            }
        }
    if (dead || won) {
        Rect ov = { f.x + f.w / 2 - 150, f.y + f.h / 2 - 60, 300, 120 };
        fill_rrect(s, ov, 16, ALPHA(HEX(0x111118), 225));
        stroke_rrect(s, ov, 16, 1, ALPHA(HEX(0xFFFFFF), 40));
        draw_text_center(s, F_UI_BOLD, 22, (Rect){ ov.x, ov.y + 14, ov.w, 30 }, won ? T.green : T.red,
                         won ? "Tebrikler, kazandın!" : "Mayına bastın!");
        if (ui_button(s, u, (Rect){ ov.x + 75, ov.y + 62, 150, 38 }, "Yeniden oyna", BTN_PRIMARY))
            setup(level);
    }
}

static void mouse(MouseEv *e)
{
    /* sağ tık: bayrak */
    if (e->kind != M_DOWN || e->button != 2 || dead || won)
        return;
    Surf *s = axsapp_surf();
    Rect f = field_rect(s);
    if (!rect_has(f, e->x, e->y))
        return;
    int x = (e->x - f.x) / CELL, y = (e->y - f.y) / CELL;
    if (open_[y][x])
        return;
    flag[y][x] = !flag[y][x];
    flags += flag[y][x] ? 1 : -1;
}

static void tick(void)
{
    if (started && !dead && !won)
        axsapp_redraw();
}

static void key(KeyEv *e)
{
    if (e->down && (e->code == KEY_N || e->code == KEY_F2))
        setup(level);
}

int main(void)
{
    srand((unsigned)time(NULL));
    setup(1);
    AxsAppFuncs fn = { .draw = draw, .mouse = mouse, .key = key, .timer = tick, .interval_ms = 1000 };
    return axsapp_run("mayin-tarlasi", "Mayın Tarlası", win_w(), win_h(), &fn);
}
