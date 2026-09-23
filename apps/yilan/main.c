/* Yılan - AxsOS oyunu */
#include "axsapp.h"

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GW 24
#define GH 18
#define CELL 24
#define TOP 56

typedef struct { int x, y; } P;
static P body[GW * GH];
static int len, dir, next_dir, score, best, state; /* 0 başla, 1 oyun, 2 duraklat, 3 bitti */
static P food;
static int speed;

static const char *best_path(void)
{
    static char p[256];
    snprintf(p, sizeof p, "%s/.yilan-rekor", getenv("HOME") ? getenv("HOME") : "/root");
    return p;
}

static void load_best(void)
{
    FILE *f = fopen(best_path(), "r");
    if (f) {
        if (fscanf(f, "%d", &best) != 1)
            best = 0;
        fclose(f);
    }
}

static void save_best(void)
{
    FILE *f = fopen(best_path(), "w");
    if (f) {
        fprintf(f, "%d\n", best);
        fclose(f);
    }
}

static int on_snake(int x, int y)
{
    for (int i = 0; i < len; i++)
        if (body[i].x == x && body[i].y == y)
            return 1;
    return 0;
}

static void place_food(void)
{
    do {
        food.x = rand() % GW;
        food.y = rand() % GH;
    } while (on_snake(food.x, food.y));
}

static void reset(void)
{
    len = 4;
    for (int i = 0; i < len; i++)
        body[i] = (P){ GW / 2 - i, GH / 2 };
    dir = next_dir = 0;
    score = 0;
    speed = 140;
    axsapp_set_interval(speed);
    place_food();
}

static void step(void)
{
    if (state != 1)
        return;
    /* ters yöne dönülemez */
    if ((next_dir + 2) % 4 != dir)
        dir = next_dir;
    static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, 1, 0, -1 };
    P h = { body[0].x + DX[dir], body[0].y + DY[dir] };
    if (h.x < 0 || h.y < 0 || h.x >= GW || h.y >= GH || on_snake(h.x, h.y)) {
        state = 3;
        if (score > best) {
            best = score;
            save_best();
        }
        axsapp_redraw();
        return;
    }
    int grow = h.x == food.x && h.y == food.y;
    if (grow && len < GW * GH)
        len++;
    memmove(&body[1], &body[0], sizeof(P) * (len - 1));
    body[0] = h;
    if (grow) {
        score += 10;
        place_food();
        if (speed > 60) {
            speed -= 4;
            axsapp_set_interval(speed);
        }
    }
    axsapp_redraw();
}

static void draw(Surf *s, UiState *u)
{
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, HEX(0x0F1A14));
    /* üst bilgi */
    char t[64];
    snprintf(t, sizeof t, "Skor  %d", score);
    draw_text(s, F_UI_BOLD, 18, 18, 16, HEX(0xA7F3D0), t);
    snprintf(t, sizeof t, "Rekor  %d", best);
    draw_text(s, F_UI_BOLD, 18, W - 18 - text_width(F_UI_BOLD, 18, t), 16, HEX(0xFDE68A), t);
    draw_text_center(s, F_UI, 12, (Rect){ 0, 18, W, 20 }, HEX(0x6B8F7B), "Oklar: yön  •  Boşluk: duraklat");
    /* alan */
    int ox = (W - GW * CELL) / 2, oy = TOP;
    Rect field = { ox, oy, GW * CELL, GH * CELL };
    fill_rrect(s, rect_inset(field, -4), 10, HEX(0x173024));
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            if ((x + y) & 1)
                fill_rect(s, (Rect){ ox + x * CELL, oy + y * CELL, CELL, CELL }, HEX(0x1A3629));
    /* yem */
    fill_circle(s, ox + food.x * CELL + CELL / 2.f, oy + food.y * CELL + CELL / 2.f + 1, CELL * 0.36f, HEX(0xEF4444));
    fill_circle(s, ox + food.x * CELL + CELL / 2.f - 3, oy + food.y * CELL + CELL / 2.f - 3, 3, ALPHA(HEX(0xFFFFFF), 150));
    draw_line(s, ox + food.x * CELL + CELL / 2.f, oy + food.y * CELL + 4.f, ox + food.x * CELL + CELL / 2.f + 4,
              oy + food.y * CELL + 1.f, 2, HEX(0x16A34A));
    /* yılan: yuvarlak kareler + komşu segmentler arası köprü (ucuz çizim) */
    for (int i = len - 1; i >= 0; i--) {
        int x = ox + body[i].x * CELL, y = oy + body[i].y * CELL;
        uint32_t c = mix(HEX(0x4ADE80), HEX(0x15803D), i * 200 / maxi(len, 1));
        if (i + 1 < len) {
            int nx = ox + body[i + 1].x * CELL, ny = oy + body[i + 1].y * CELL;
            Rect br = { mini(x, nx) + 3, mini(y, ny) + 3, abs(nx - x) + CELL - 6, abs(ny - y) + CELL - 6 };
            fill_rect(s, br, c);
        }
        fill_rrect(s, (Rect){ x + 2, y + 2, CELL - 4, CELL - 4 }, 8, c);
    }
    /* baş ve gözler */
    {
        static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, 1, 0, -1 };
        float cx = ox + body[0].x * CELL + CELL / 2.f, cy = oy + body[0].y * CELL + CELL / 2.f;
        float px = -DY[dir], py = DX[dir];
        for (int k = -1; k <= 1; k += 2) {
            float ex = cx + DX[dir] * 4 + px * 5 * k, ey = cy + DY[dir] * 4 + py * 5 * k;
            fill_circle(s, ex, ey, 3.2f, HEX(0xFFFFFF));
            fill_circle(s, ex + DX[dir], ey + DY[dir], 1.6f, HEX(0x0B0B0B));
        }
    }
    /* katman: başla / duraklat / bitti */
    if (state != 1) {
        fill_rect(s, field, ALPHA(HEX(0x000000), 150));
        const char *title = state == 0 ? "Yılan" : state == 2 ? "Duraklatıldı" : "Oyun bitti!";
        draw_text_center(s, F_UI_BOLD, 34, (Rect){ field.x, field.y + field.h / 2 - 90, field.w, 50 }, HEX(0xFFFFFF), title);
        if (state == 3) {
            snprintf(t, sizeof t, "Skorun: %d%s", score, score >= best && score > 0 ? "  —  yeni rekor!" : "");
            draw_text_center(s, F_UI, 16, (Rect){ field.x, field.y + field.h / 2 - 36, field.w, 24 }, HEX(0xFDE68A), t);
        }
        Rect b = { field.x + field.w / 2 - 90, field.y + field.h / 2 + 4, 180, 44 };
        if (ui_button(s, u, b, state == 2 ? "Devam et" : state == 3 ? "Yeniden oyna" : "Başla", BTN_PRIMARY)) {
            if (state != 2)
                reset();
            state = 1;
        }
        draw_text_center(s, F_UI, 12, (Rect){ field.x, b.y + 56, field.w, 18 }, ALPHA(HEX(0xFFFFFF), 160),
                         "ya da Enter'a bas");
    }
}

static void key(KeyEv *e)
{
    if (!e->down)
        return;
    switch (e->code) {
    case KEY_RIGHT: case KEY_D: next_dir = 0; break;
    case KEY_DOWN: case KEY_S: next_dir = 1; break;
    case KEY_LEFT: case KEY_A: next_dir = 2; break;
    case KEY_UP: case KEY_W: next_dir = 3; break;
    case KEY_SPACE: case KEY_P:
        if (state == 1) state = 2;
        else if (state == 2) state = 1;
        break;
    case KEY_ENTER: case KEY_KPENTER:
        if (state != 1) {
            if (state != 2)
                reset();
            state = 1;
        }
        break;
    }
}

int main(void)
{
    srand((unsigned)time(NULL));
    load_best();
    reset();
    AxsAppFuncs f = { .draw = draw, .key = key, .timer = step, .interval_ms = 140 };
    return axsapp_run("yilan", "Yılan", GW * CELL + 40, GH * CELL + TOP + 24, &f);
}
