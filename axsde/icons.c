/* AxsDE - prosedürel simgeler (vektör şekillerden çizilir, her boyutta keskin) */
#include "axsde.h"

#include <math.h>

/* Kenar yumuşatmalı üçgen: 4x4 alt örnekleme */
static void fill_triangle(Surf *s, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c)
{
    int bx0 = (int)floorf(fminf(x0, fminf(x1, x2))), by0 = (int)floorf(fminf(y0, fminf(y1, y2)));
    int bx1 = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))), by1 = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    Rect k = rect_isect((Rect){ bx0, by0, bx1 - bx0 + 1, by1 - by0 + 1 }, s->clip);
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(area) < 0.01f)
        return;
    float sg = area > 0 ? 1.f : -1.f;
    for (int y = k.y; y < k.y + k.h; y++)
        for (int x = k.x; x < k.x + k.w; x++) {
            int in = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    float px = x + (i + 0.5f) / 4, py = y + (j + 0.5f) / 4;
                    float e0 = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) * sg;
                    float e1 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) * sg;
                    float e2 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) * sg;
                    if (e0 >= 0 && e1 >= 0 && e2 >= 0)
                        in++;
                }
            if (in)
                fill_rect(s, (Rect){ x, y, 1, 1 }, ALPHA(c, CA(c) * in / 16));
        }
}

static void squircle(Surf *s, int x, int y, int sz, uint32_t top, uint32_t bot)
{
    Rect r = { x, y, sz, sz };
    int rad = sz * 23 / 100;
    fill_rrect_vgrad(s, r, rad, top, bot);
    /* üstte ince parlama */
    fill_rrect_corners(s, (Rect){ x, y, sz, sz / 2 }, rad, ALPHA(HEX(0xFFFFFF), 22), 3);
    stroke_rrect(s, r, rad, 1, ALPHA(HEX(0xFFFFFF), 30));
}

void draw_icon(Surf *s, IconId id, int x, int y, int sz)
{
    float f = (float)sz;
    float cx = x + f / 2, cy = y + f / 2;
    uint32_t W = HEX(0xFFFFFF);
    uint32_t sub = T.subtext;
    switch (id) {
    /* ---- uygulama simgeleri (arka planlı) ---- */
    case IC_TERMINAL:
        squircle(s, x, y, sz, HEX(0x3A3A52), HEX(0x14141E));
        draw_line(s, x + f * .26f, y + f * .34f, x + f * .44f, y + f * .50f, f * .075f, HEX(0x34D399));
        draw_line(s, x + f * .44f, y + f * .50f, x + f * .26f, y + f * .66f, f * .075f, HEX(0x34D399));
        draw_line(s, x + f * .52f, y + f * .68f, x + f * .74f, y + f * .68f, f * .075f, W);
        break;
    case IC_FILES:
        squircle(s, x, y, sz, HEX(0x60A5FA), HEX(0x1D4ED8));
        fill_rrect(s, (Rect){ x + sz * 22 / 100, y + sz * 28 / 100, sz * 24 / 100, sz * 14 / 100 }, sz / 20 + 1, HEX(0xBFDBFE));
        fill_rrect_vgrad(s, (Rect){ x + sz * 22 / 100, y + sz * 35 / 100, sz * 56 / 100, sz * 38 / 100 }, sz / 16 + 1, HEX(0xEFF6FF), HEX(0xBFDBFE));
        break;
    case IC_STUDIO:
        squircle(s, x, y, sz, T.accent2, lighten(T.accent, -60));
        draw_logo(s, cx, cy + f * .02f, f * .58f, W, HEX(0xE9D5FF));
        break;
    case IC_PACKAGES:
        squircle(s, x, y, sz, HEX(0xFBBF24), HEX(0xD97706));
        fill_rrect_vgrad(s, (Rect){ x + sz * 24 / 100, y + sz * 30 / 100, sz * 52 / 100, sz * 44 / 100 }, sz / 18 + 1, HEX(0xFFFBEB), HEX(0xFDE68A));
        fill_rect(s, (Rect){ x + sz * 24 / 100, y + sz * 40 / 100, sz * 52 / 100, maxi(1, sz / 28) }, HEX(0xD97706));
        fill_rect(s, (Rect){ x + sz * 46 / 100, y + sz * 30 / 100, sz * 8 / 100, sz * 16 / 100 }, HEX(0xF59E0B));
        break;
    case IC_MONITOR:
        squircle(s, x, y, sz, HEX(0x34D399), HEX(0x047857));
        {
            float pts[][2] = { { .22f, .66f }, { .38f, .50f }, { .50f, .60f }, { .64f, .34f }, { .78f, .44f } };
            for (int i = 0; i < 4; i++)
                draw_line(s, x + f * pts[i][0], y + f * pts[i][1], x + f * pts[i + 1][0], y + f * pts[i + 1][1], f * .07f, W);
            fill_circle(s, x + f * .64f, y + f * .34f, f * .06f, W);
        }
        break;
    case IC_SETTINGS:
        squircle(s, x, y, sz, HEX(0x94A3B8), HEX(0x334155));
        for (int i = 0; i < 8; i++) {
            float a = (float)i * 3.14159265f / 4;
            draw_line(s, cx + cosf(a) * f * .18f, cy + sinf(a) * f * .18f, cx + cosf(a) * f * .29f, cy + sinf(a) * f * .29f, f * .11f, W);
        }
        fill_circle(s, cx, cy, f * .22f, W);
        fill_circle(s, cx, cy, f * .09f, HEX(0x64748B));
        break;
    case IC_ABOUT:
        squircle(s, x, y, sz, HEX(0xF472B6), HEX(0x7C3AED));
        fill_circle(s, cx, y + f * .30f, f * .065f, W);
        draw_line(s, cx, y + f * .45f, cx, y + f * .72f, f * .12f, W);
        break;

    /* ---- küçük simgeler (arka plansız) ---- */
    case IC_FOLDER:
        fill_rrect(s, (Rect){ x + sz * 6 / 100, y + sz * 16 / 100, sz * 40 / 100, sz * 20 / 100 }, sz / 12 + 1, HEX(0x3B82F6));
        fill_rrect_vgrad(s, (Rect){ x + sz * 6 / 100, y + sz * 26 / 100, sz * 88 / 100, sz * 62 / 100 }, sz / 10 + 1, HEX(0x93C5FD), HEX(0x3B82F6));
        break;
    case IC_FILE:
    case IC_AXSFILE:
    case IC_EXEC: {
        Rect pg = { x + sz * 18 / 100, y + sz * 8 / 100, sz * 64 / 100, sz * 84 / 100 };
        fill_rrect(s, pg, sz / 10 + 1, id == IC_AXSFILE ? HEX(0xEDE9FE) : HEX(0xE2E8F0));
        fill_triangle(s, pg.x + pg.w - f * .22f, (float)pg.y, (float)(pg.x + pg.w), (float)pg.y, (float)(pg.x + pg.w), pg.y + f * .22f, T.surface);
        fill_triangle(s, pg.x + pg.w - f * .22f, (float)pg.y, pg.x + pg.w - f * .22f, pg.y + f * .22f, (float)(pg.x + pg.w), pg.y + f * .22f, HEX(0x94A3B8));
        if (id == IC_AXSFILE) {
            draw_logo(s, cx, cy + f * .08f, f * .42f, T.accent, lighten(T.accent, -40));
        } else if (id == IC_EXEC) {
            draw_line(s, x + f * .32f, y + f * .46f, x + f * .44f, y + f * .56f, f * .07f, HEX(0x059669));
            draw_line(s, x + f * .44f, y + f * .56f, x + f * .32f, y + f * .66f, f * .07f, HEX(0x059669));
        } else {
            for (int i = 0; i < 3; i++)
                fill_rect(s, (Rect){ pg.x + sz * 12 / 100, pg.y + sz * (36 + i * 14) / 100, sz * (40 - i * 8) / 100, maxi(1, sz / 16) }, HEX(0x94A3B8));
        }
        break;
    }
    case IC_PACKAGE:
        fill_rrect_vgrad(s, (Rect){ x + sz * 12 / 100, y + sz * 24 / 100, sz * 76 / 100, sz * 62 / 100 }, sz / 10 + 1, HEX(0xFCD34D), HEX(0xD97706));
        fill_rect(s, (Rect){ x + sz * 12 / 100, y + sz * 38 / 100, sz * 76 / 100, maxi(1, sz / 18) }, HEX(0x92400E));
        fill_rect(s, (Rect){ x + sz * 44 / 100, y + sz * 24 / 100, sz * 12 / 100, sz * 22 / 100 }, HEX(0xFEF3C7));
        break;
    case IC_POWER:
        stroke_circle(s, cx, cy + f * .03f, f * .30f, f * .10f, sub);
        draw_line(s, cx, y + f * .12f, cx, y + f * .46f, f * .10f, sub);
        break;
    case IC_SEARCH:
        stroke_circle(s, x + f * .43f, y + f * .43f, f * .24f, f * .09f, sub);
        draw_line(s, x + f * .61f, y + f * .61f, x + f * .82f, y + f * .82f, f * .11f, sub);
        break;
    case IC_NETWORK:
        for (int i = 0; i < 4; i++) {
            int bh = sz * (22 + i * 18) / 100;
            fill_rrect(s, (Rect){ x + sz * (12 + i * 21) / 100, y + sz * 86 / 100 - bh, sz * 15 / 100, bh }, sz / 20 + 1, sub);
        }
        break;
    case IC_PLAY:
        fill_triangle(s, x + f * .28f, y + f * .18f, x + f * .28f, y + f * .82f, x + f * .82f, y + f * .50f, HEX(0x34D399));
        break;
    case IC_SAVE:
        stroke_rrect(s, (Rect){ x + sz * 14 / 100, y + sz * 14 / 100, sz * 72 / 100, sz * 72 / 100 }, sz / 10 + 1, maxi(1, sz / 12), sub);
        fill_rect(s, (Rect){ x + sz * 30 / 100, y + sz * 14 / 100, sz * 36 / 100, sz * 22 / 100 }, sub);
        fill_rrect(s, (Rect){ x + sz * 30 / 100, y + sz * 56 / 100, sz * 40 / 100, sz * 20 / 100 }, 2, sub);
        break;
    case IC_UP:
        draw_line(s, cx, y + f * .20f, cx, y + f * .80f, f * .10f, sub);
        draw_line(s, cx, y + f * .20f, x + f * .26f, y + f * .44f, f * .10f, sub);
        draw_line(s, cx, y + f * .20f, x + f * .74f, y + f * .44f, f * .10f, sub);
        break;
    case IC_HOME:
        fill_triangle(s, x + f * .12f, y + f * .50f, cx, y + f * .14f, x + f * .88f, y + f * .50f, sub);
        fill_rrect(s, (Rect){ x + sz * 24 / 100, y + sz * 46 / 100, sz * 52 / 100, sz * 40 / 100 }, 2, sub);
        break;
    case IC_REFRESH:
        stroke_circle(s, cx, cy, f * .28f, f * .09f, sub);
        fill_triangle(s, x + f * .56f, y + f * .10f, x + f * .84f, y + f * .26f, x + f * .56f, y + f * .40f, sub);
        break;
    case IC_NEW:
        draw_line(s, cx, y + f * .20f, cx, y + f * .80f, f * .11f, sub);
        draw_line(s, x + f * .20f, cy, x + f * .80f, cy, f * .11f, sub);
        break;
    case IC_TRASH:
        fill_rrect(s, (Rect){ x + sz * 18 / 100, y + sz * 18 / 100, sz * 64 / 100, sz * 10 / 100 }, 2, sub);
        fill_rrect(s, (Rect){ x + sz * 26 / 100, y + sz * 32 / 100, sz * 48 / 100, sz * 56 / 100 }, sz / 12 + 1, sub);
        break;
    case IC_KEYBOARD:
        stroke_rrect(s, (Rect){ x + sz * 8 / 100, y + sz * 24 / 100, sz * 84 / 100, sz * 52 / 100 }, sz / 10 + 1, maxi(1, sz / 14), sub);
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 4; i++)
                fill_rect(s, (Rect){ x + sz * (20 + i * 17) / 100, y + sz * (36 + j * 14) / 100, maxi(1, sz / 10), maxi(1, sz / 12) }, sub);
        break;
    case IC_CLOCK:
        stroke_circle(s, cx, cy, f * .36f, f * .08f, sub);
        draw_line(s, cx, cy, cx, y + f * .28f, f * .08f, sub);
        draw_line(s, cx, cy, x + f * .66f, y + f * .58f, f * .08f, sub);
        break;
    case IC_MEMORY:
    case IC_CPU:
        fill_rrect(s, (Rect){ x + sz * 22 / 100, y + sz * 22 / 100, sz * 56 / 100, sz * 56 / 100 }, sz / 10 + 1, sub);
        for (int i = 0; i < 3; i++) {
            int p = sz * (32 + i * 16) / 100;
            fill_rect(s, (Rect){ x + p, y + sz * 8 / 100, maxi(1, sz / 14), sz * 12 / 100 }, sub);
            fill_rect(s, (Rect){ x + p, y + sz * 80 / 100, maxi(1, sz / 14), sz * 12 / 100 }, sub);
            if (id == IC_CPU) {
                fill_rect(s, (Rect){ x + sz * 8 / 100, y + p, sz * 12 / 100, maxi(1, sz / 14) }, sub);
                fill_rect(s, (Rect){ x + sz * 80 / 100, y + p, sz * 12 / 100, maxi(1, sz / 14) }, sub);
            }
        }
        fill_rrect(s, (Rect){ x + sz * 36 / 100, y + sz * 36 / 100, sz * 28 / 100, sz * 28 / 100 }, 2, T.surface);
        break;
    default:
        break;
    }
}
