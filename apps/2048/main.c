/* 2048 - AxsOS oyunu */
#include "axsapp.h"

#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g[4][4], score, best, won, over, keep_going;

static const char *best_path(void)
{
    static char p[256];
    snprintf(p, sizeof p, "%s/.2048-rekor", getenv("HOME") ? getenv("HOME") : "/root");
    return p;
}

static void save_best(void)
{
    FILE *f = fopen(best_path(), "w");
    if (f) {
        fprintf(f, "%d\n", best);
        fclose(f);
    }
}

static void spawn(void)
{
    int free_[16], n = 0;
    for (int i = 0; i < 16; i++)
        if (!g[i / 4][i % 4])
            free_[n++] = i;
    if (!n)
        return;
    int k = free_[rand() % n];
    g[k / 4][k % 4] = rand() % 10 ? 2 : 4;
}

static void new_game(void)
{
    memset(g, 0, sizeof g);
    score = won = over = keep_going = 0;
    spawn();
    spawn();
}

/* Bir satırı sola kaydır/birleştir; değiştiyse 1 */
static int slide(int *row)
{
    int out[4] = { 0 }, n = 0, moved = 0;
    for (int i = 0; i < 4; i++) {
        if (!row[i])
            continue;
        if (n && out[n - 1] == row[i] && out[n - 1] > 0) {
            out[n - 1] *= -2; /* bu turda birleşti (negatif işaret) */
            score += -out[n - 1];
        } else {
            out[n++] = row[i];
        }
    }
    for (int i = 0; i < 4; i++) {
        if (out[i] < 0)
            out[i] = -out[i];
        if (out[i] != row[i])
            moved = 1;
        row[i] = out[i];
    }
    return moved;
}

static int can_move(void)
{
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            if (!g[y][x])
                return 1;
            if (x < 3 && g[y][x] == g[y][x + 1])
                return 1;
            if (y < 3 && g[y][x] == g[y + 1][x])
                return 1;
        }
    return 0;
}

static void move(int d) /* 0 sol, 1 sağ, 2 yukarı, 3 aşağı */
{
    if (over || (won && !keep_going))
        return;
    int moved = 0;
    for (int i = 0; i < 4; i++) {
        int row[4];
        for (int j = 0; j < 4; j++) {
            int x = d < 2 ? (d == 0 ? j : 3 - j) : i, y = d < 2 ? i : (d == 2 ? j : 3 - j);
            row[j] = g[y][x];
        }
        moved |= slide(row);
        for (int j = 0; j < 4; j++) {
            int x = d < 2 ? (d == 0 ? j : 3 - j) : i, y = d < 2 ? i : (d == 2 ? j : 3 - j);
            g[y][x] = row[j];
        }
    }
    if (!moved)
        return;
    spawn();
    for (int i = 0; i < 16; i++)
        if (g[i / 4][i % 4] == 2048 && !keep_going)
            won = 1;
    if (!can_move())
        over = 1;
    if (score > best) {
        best = score;
        save_best();
    }
}

static uint32_t tile_color(int v)
{
    switch (v) {
    case 2: return HEX(0xEEE4DA);
    case 4: return HEX(0xEDE0C8);
    case 8: return HEX(0xF2B179);
    case 16: return HEX(0xF59563);
    case 32: return HEX(0xF67C5F);
    case 64: return HEX(0xF65E3B);
    case 128: return HEX(0xEDCF72);
    case 256: return HEX(0xEDCC61);
    case 512: return HEX(0xEDC850);
    case 1024: return HEX(0xEDC53F);
    case 2048: return HEX(0xEDC22E);
    default: return HEX(0x3C3A32);
    }
}

static void score_box(Surf *s, Rect r, const char *label, int v)
{
    fill_rrect(s, r, 10, HEX(0xBBADA0));
    draw_text_center(s, F_UI_BOLD, 11, (Rect){ r.x, r.y + 6, r.w, 14 }, HEX(0xEEE4DA), label);
    char t[16];
    snprintf(t, sizeof t, "%d", v);
    draw_text_center(s, F_UI_BOLD, 20, (Rect){ r.x, r.y + 22, r.w, 26 }, HEX(0xFFFFFF), t);
}

static void draw(Surf *s, UiState *u)
{
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, HEX(0xFAF8EF));
    draw_text(s, F_UI_BOLD, 44, 20, 12, HEX(0x776E65), "2048");
    score_box(s, (Rect){ W - 200, 16, 86, 54 }, "SKOR", score);
    score_box(s, (Rect){ W - 106, 16, 86, 54 }, "EN İYİ", best);
    draw_text(s, F_UI, 13, 22, 80, HEX(0x8F8579), "Taşları birleştir, 2048'e ulaş!");
    if (ui_button(s, u, (Rect){ W - 140, 80, 120, 32 }, "Yeni oyun", BTN_PRIMARY))
        new_game();
    int top = 124, pad = 12;
    int size = mini(W - 40, H - top - 20);
    Rect board = { (W - size) / 2, top, size, size };
    fill_rrect(s, board, 12, HEX(0xBBADA0));
    int cs = (size - 5 * pad) / 4;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            Rect c = { board.x + pad + x * (cs + pad), board.y + pad + y * (cs + pad), cs, cs };
            int v = g[y][x];
            fill_rrect(s, c, 8, v ? tile_color(v) : HEX(0xCDC1B4));
            if (!v)
                continue;
            char t[8];
            snprintf(t, sizeof t, "%d", v);
            int fs = v < 100 ? 38 : v < 1000 ? 32 : 26;
            draw_text_center(s, F_UI_BOLD, fs, c, v <= 4 ? HEX(0x776E65) : HEX(0xF9F6F2), t);
        }
    if (over || (won && !keep_going)) {
        fill_rrect(s, board, 12, ALPHA(won ? HEX(0xEDC22E) : HEX(0xEEE4DA), 190));
        draw_text_center(s, F_UI_BOLD, 40, (Rect){ board.x, board.y + board.h / 2 - 70, board.w, 50 },
                         won ? HEX(0xF9F6F2) : HEX(0x776E65), won ? "Kazandın!" : "Oyun bitti");
        Rect b = { board.x + board.w / 2 - 80, board.y + board.h / 2, 160, 40 };
        if (won && !over) {
            if (ui_button(s, u, (Rect){ b.x - 90, b.y, 160, 40 }, "Devam et", BTN_NORMAL))
                keep_going = 1;
            b.x += 90;
        }
        if (ui_button(s, u, b, "Yeniden dene", BTN_PRIMARY))
            new_game();
    }
}

static void key(KeyEv *e)
{
    if (!e->down)
        return;
    switch (e->code) {
    case KEY_LEFT: case KEY_A: move(0); break;
    case KEY_RIGHT: case KEY_D: move(1); break;
    case KEY_UP: case KEY_W: move(2); break;
    case KEY_DOWN: case KEY_S: move(3); break;
    case KEY_N: new_game(); break;
    }
}

int main(void)
{
    srand((unsigned)time(NULL));
    FILE *f = fopen(best_path(), "r");
    if (f) {
        if (fscanf(f, "%d", &best) != 1)
            best = 0;
        fclose(f);
    }
    new_game();
    AxsAppFuncs fn = { .draw = draw, .key = key };
    return axsapp_run("2048", "2048", 440, 560, &fn);
}
