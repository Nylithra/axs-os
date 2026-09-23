/* Hesap Makinesi - AxsOS */
#include "axsapp.h"

#include <linux/input.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char expr[128];     /* girilen ifade, ör. "12+3×4" */
static char result[64];    /* son sonuç */
static char prev[160];     /* üstte küçük: önceki işlem */
static int just_done;      /* '=' sonrası yeni sayı yazılırsa ifade sıfırlansın */

/* ---- ifade çözümleyici (öncelikli, parantezli) ---- */
static const char *pp;
static int perr;
static double p_expr(void);

static void skip(void) { while (*pp == ' ') pp++; }

static double p_num(void)
{
    skip();
    if (*pp == '(') {
        pp++;
        double v = p_expr();
        skip();
        if (*pp == ')')
            pp++;
        return v;
    }
    if (*pp == '-') {
        pp++;
        return -p_num();
    }
    char buf[64];
    int n = 0;
    while (((*pp >= '0' && *pp <= '9') || *pp == ',' || *pp == '.') && n < 63)
        buf[n++] = *pp == ',' ? '.' : *pp, pp++;
    buf[n] = 0;
    if (!n) {
        perr = 1;
        return 0;
    }
    double v = atof(buf);
    if (*pp == '%') { /* yüzde */
        pp++;
        v /= 100;
    }
    return v;
}

static double p_term(void)
{
    double v = p_num();
    for (;;) {
        skip();
        /* × ve ÷ UTF-8 olarak iki bayt */
        if (*pp == '*' || !strncmp(pp, "×", 2)) {
            pp += *pp == '*' ? 1 : 2;
            v *= p_num();
        } else if (*pp == '/' || !strncmp(pp, "÷", 2)) {
            pp += *pp == '/' ? 1 : 2;
            double d = p_num();
            if (d == 0)
                perr = 2;
            else
                v /= d;
        } else {
            return v;
        }
    }
}

static double p_expr(void)
{
    double v = p_term();
    for (;;) {
        skip();
        if (*pp == '+') {
            pp++;
            v += p_term();
        } else if (*pp == '-' || !strncmp(pp, "−", 3)) {
            pp += *pp == '-' ? 1 : 3;
            v -= p_term();
        } else {
            return v;
        }
    }
}

static void fmt(double v, char *out, size_t n)
{
    if (fabs(v) >= 1e15 || (fabs(v) < 1e-9 && v != 0)) {
        snprintf(out, n, "%.6g", v);
    } else {
        snprintf(out, n, "%.10f", v);
        char *d = strchr(out, '.');
        if (d) {
            char *e = out + strlen(out) - 1;
            while (e > d && *e == '0')
                *e-- = 0;
            if (e == d)
                *e = 0;
        }
    }
    for (char *c = out; *c; c++)
        if (*c == '.')
            *c = ','; /* Türkçe ondalık ayırıcı */
}

static void evaluate(void)
{
    if (!expr[0])
        return;
    pp = expr;
    perr = 0;
    double v = p_expr();
    skip();
    if (*pp)
        perr = 1;
    snprintf(prev, sizeof prev, "%s =", expr);
    if (perr == 2)
        snprintf(result, sizeof result, "Sıfıra bölünemez");
    else if (perr)
        snprintf(result, sizeof result, "Hatalı ifade");
    else {
        fmt(v, result, sizeof result);
        snprintf(expr, sizeof expr, "%s", result);
    }
    just_done = 1;
}

static void append(const char *t)
{
    int is_op = !strcmp(t, "+") || !strcmp(t, "−") || !strcmp(t, "×") || !strcmp(t, "÷") || !strcmp(t, "%");
    if (just_done && !is_op)
        expr[0] = 0;
    just_done = 0;
    result[0] = 0;
    if (strlen(expr) + strlen(t) < sizeof expr - 1)
        strcat(expr, t);
}

static void backspace(void)
{
    int n = (int)strlen(expr);
    if (!n)
        return;
    n = utf8_prev(expr, n);
    expr[n] = 0;
    just_done = 0;
}

static void clear_all(void)
{
    expr[0] = result[0] = prev[0] = 0;
    just_done = 0;
}

static void press(const char *k)
{
    if (!strcmp(k, "C")) clear_all();
    else if (!strcmp(k, "⌫")) backspace();
    else if (!strcmp(k, "=")) evaluate();
    else if (!strcmp(k, "±")) {
        char tmp[140];
        snprintf(tmp, sizeof tmp, "-(%s)", expr[0] ? expr : "0");
        snprintf(expr, sizeof expr, "%s", tmp);
        just_done = 0;
    } else append(k);
}

static const char *KEYS[5][4] = {
    { "C", "⌫", "%", "÷" },
    { "7", "8", "9", "×" },
    { "4", "5", "6", "−" },
    { "1", "2", "3", "+" },
    { "±", "0", ",", "=" },
};

static void draw(Surf *s, UiState *u)
{
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, T.base);
    /* ekran */
    Rect disp = { 16, 16, W - 32, 110 };
    fill_rrect_vgrad(s, disp, 16, T.surface2, T.surface);
    stroke_rrect(s, disp, 16, 1, ALPHA(HEX(0xFFFFFF), 16));
    draw_text_fit(s, F_UI, 14, disp.x + 16, disp.y + 12, disp.w - 32, T.muted, prev);
    const char *big = expr[0] ? expr : "0";
    int size = 40;
    while (size > 18 && text_width(F_UI_BOLD, size, big) > disp.w - 32)
        size -= 2;
    int tw = text_width(F_UI_BOLD, size, big);
    draw_text(s, F_UI_BOLD, size, disp.x + disp.w - 16 - tw, disp.y + disp.h - 16 - font_height(F_UI_BOLD, size),
              result[0] && !just_done ? T.subtext : T.text, big);
    if (result[0] && just_done && strcmp(result, expr))
        draw_text(s, F_UI, 13, disp.x + 16, disp.y + 34, T.red, result);
    /* tuşlar */
    int gy = disp.y + disp.h + 14, gap = 10;
    int bw = (W - 32 - 3 * gap) / 4, bh = (H - gy - 16 - 4 * gap) / 5;
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 4; c++) {
            const char *k = KEYS[r][c];
            Rect b = { 16 + c * (bw + gap), gy + r * (bh + gap), bw, bh };
            int op = c == 3, eq = !strcmp(k, "="), fn = r == 0 && c < 3;
            int hov = ui_hover(u, b), act = hov && u->down;
            uint32_t bg = eq ? T.accent : op ? T.overlay : fn ? T.surface2 : T.surface;
            if (act)
                bg = lighten(bg, -25);
            else if (hov)
                bg = lighten(bg, 22);
            if (eq)
                fill_rrect_vgrad(s, b, 14, lighten(bg, 20), bg);
            else
                fill_rrect(s, b, 14, bg);
            stroke_rrect(s, b, 14, 1, ALPHA(HEX(0xFFFFFF), 12));
            draw_text_center(s, F_UI_BOLD, op || eq ? 22 : 20, b, eq ? HEX(0xFFFFFF) : op ? T.accent2 : T.text, k);
            if (ui_clicked(u, b))
                press(k);
        }
}

static void key(KeyEv *e)
{
    if (!e->down)
        return;
    switch (e->code) {
    case KEY_ENTER: case KEY_KPENTER: press("="); return;
    case KEY_BACKSPACE: press("⌫"); return;
    case KEY_ESC: case KEY_DELETE: press("C"); return;
    case KEY_KPPLUS: press("+"); return;
    case KEY_KPMINUS: press("−"); return;
    case KEY_KPASTERISK: press("×"); return;
    case KEY_KPSLASH: press("÷"); return;
    }
    uint32_t c = e->ch;
    if (c >= '0' && c <= '9') {
        char t[2] = { (char)c, 0 };
        press(t);
    } else if (c == ',' || c == '.') press(",");
    else if (c == '+') press("+");
    else if (c == '-') press("−");
    else if (c == '*' || c == 'x') press("×");
    else if (c == '/') press("÷");
    else if (c == '%') press("%");
    else if (c == '(' || c == ')') {
        char t[2] = { (char)c, 0 };
        append(t);
    } else if (c == '=') press("=");
}

int main(void)
{
    AxsAppFuncs f = { .draw = draw, .key = key };
    return axsapp_run("hesap-makinesi", "Hesap Makinesi", 340, 520, &f);
}
