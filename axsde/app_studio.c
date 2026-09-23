/* AxsDE - Axs Stüdyo: Axs için kod editörü + tek tıkla çalıştırma */
#include "axsde.h"

#include <errno.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FS 14          /* yazı boyutu */
#define LH 21          /* satır yüksekliği */
#define GUTTER 58
#define TOOL_H 50
#define STATUS_H 26
#define OUT_H 210

typedef struct { char *s; int len, cap; } Line;

typedef struct {
    UiState ui;
    Line *ln;
    int n, cap;
    int cl, cc;          /* imleç satırı ve bayt sütunu */
    int want;            /* dikey harekette tercih edilen karakter sütunu */
    int top, hx;         /* kaydırma: ilk satır, yatay karakter kayması */
    char path[PATH_MAX];
    int has_path, modified;
    Term *out;
    int out_open, out_focus, out_done;
    int blink_on, blink_t;
    char msg[160];
    long msg_until;
    /* hızlı yol: yalnızca bu satırları + durum çubuğunu yeniden çiz */
    int fast_ok, fast_n, fast_lines[2];
} St;

static const char *SAMPLE =
    "# Axs Stüdyo'ya hoş geldin!\n"
    "# Kodu yaz, ▶ Çalıştır'a bas (ya da F5 / Ctrl+R).\n"
    "\n"
    "ad = \"AxsOS\"\n"
    "print: Merhaba, ad;!\n"
    "\n"
    "func kare(x)\n"
    "  return x * x\n"
    "end\n"
    "\n"
    "for i in [1, 2, 3, 4, 5]\n"
    "  print: i; x i; = (kare(i));\n"
    "end\n"
    "\n"
    "meyveler = [\"elma\", \"armut\", \"kiraz\"]\n"
    "print: Meyve sayısı: (meyveler.len());\n"
    "\n"
    "if kare(7) > 40\n"
    "  print: 7'nin karesi 40'tan büyük.\n"
    "else\n"
    "  print: Küçük!\n"
    "end\n";

static const char *KEYWORDS[] = {
    "if", "eger", "eğer", "elif", "yoksa", "veyaeger", "else", "degilse", "değilse", "end", "son", "bitir",
    "while", "surece", "sürece", "repeat", "tekrar", "for", "her", "in", "icinde", "içinde", "func", "is",
    "iş", "fonksiyon", "return", "dondur", "döndür", "stop", "dur", "skip", "atla", "try", "dene", "catch",
    "yakala", "use", "kullan", "and", "ve", "or", "veya", "not", "degil", "değil", "mod", "kalan", "true",
    "dogru", "doğru", "evet", "false", "yanlis", "yanlış", "hayir", "hayır", "null", "bos", "boş", "yok", NULL
};
static const char *BLOCK_START[] = { "if", "eger", "eğer", "elif", "yoksa", "else", "degilse", "değilse", "while",
                                     "surece", "sürece", "repeat", "tekrar", "for", "her", "func", "is", "iş",
                                     "fonksiyon", "try", "dene", "catch", "yakala", NULL };

#define C_KW   T.accent2
#define C_STR  HEX(0xA6E3A1)
#define C_NUM  HEX(0xFAB387)
#define C_FN   HEX(0x89B4FA)
#define C_COM  HEX(0x6C7086)
#define C_OP   HEX(0x94E2D5)
#define C_VAR  HEX(0xF9E2AF)
#define C_TPL  HEX(0xE6E9F5)

/* ------------------------------------------------------------------ */
/* Metin tamponu                                                       */
/* ------------------------------------------------------------------ */

static void line_set(Line *l, const char *s, int n)
{
    if (n + 1 > l->cap) {
        l->cap = n + 32;
        l->s = realloc(l->s, l->cap);
    }
    memcpy(l->s, s, n);
    l->len = n;
    l->s[n] = 0;
}

static void line_insert(Line *l, int at, const char *s, int n)
{
    if (l->len + n + 1 > l->cap) {
        l->cap = (l->len + n + 1) * 2;
        l->s = realloc(l->s, l->cap);
    }
    memmove(l->s + at + n, l->s + at, l->len - at + 1);
    memcpy(l->s + at, s, n);
    l->len += n;
}

static void line_delete(Line *l, int at, int n)
{
    memmove(l->s + at, l->s + at + n, l->len - at - n + 1);
    l->len -= n;
}

static void lines_insert(St *st, int at)
{
    if (st->n + 1 > st->cap) {
        st->cap = (st->n + 1) * 2;
        st->ln = realloc(st->ln, sizeof(Line) * st->cap);
    }
    memmove(&st->ln[at + 1], &st->ln[at], sizeof(Line) * (st->n - at));
    st->ln[at] = (Line){ 0 };
    line_set(&st->ln[at], "", 0);
    st->n++;
}

static void lines_remove(St *st, int at)
{
    free(st->ln[at].s);
    memmove(&st->ln[at], &st->ln[at + 1], sizeof(Line) * (st->n - at - 1));
    st->n--;
}

static void set_text(St *st, const char *txt)
{
    for (int i = 0; i < st->n; i++)
        free(st->ln[i].s);
    st->n = 0;
    const char *p = txt;
    do {
        size_t k = strcspn(p, "\n");
        lines_insert(st, st->n);
        /* sekmeleri iki boşluğa çevir */
        Line *l = &st->ln[st->n - 1];
        for (size_t i = 0; i < k; i++) {
            if (p[i] == '\t')
                line_insert(l, l->len, "  ", 2);
            else if (p[i] != '\r')
                line_insert(l, l->len, &p[i], 1);
        }
        p += k;
        if (*p == '\n')
            p++;
        else
            break;
    } while (1);
    if (st->n > 1 && st->ln[st->n - 1].len == 0)
        lines_remove(st, st->n - 1);
    st->cl = st->cc = st->top = st->hx = 0;
}

static int char_col(const char *s, int byte)
{
    int c = 0;
    for (int i = 0; i < byte; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80)
            c++;
    return c;
}

static int col_byte(const Line *l, int col)
{
    int i = 0, c = 0;
    while (i < l->len && c < col) {
        i++;
        while (i < l->len && ((unsigned char)l->s[i] & 0xC0) == 0x80)
            i++;
        c++;
    }
    return i;
}

/* ------------------------------------------------------------------ */
/* Dosya işlemleri                                                     */
/* ------------------------------------------------------------------ */

static void flash(St *st, const char *m)
{
    snprintf(st->msg, sizeof st->msg, "%s", m);
    st->msg_until = now_ms() + 3500;
}

static void update_title(Win *w)
{
    St *st = w->st;
    const char *b = st->has_path ? strrchr(st->path, '/') : NULL;
    char t[160];
    snprintf(t, sizeof t, "%s%s — Axs Stüdyo", st->modified ? "• " : "", b ? b + 1 : "adsız.axs");
    wm_set_title(w, t);
}

static int load_file(Win *w, const char *path)
{
    St *st = w->st;
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n > 4 * 1024 * 1024) {
        fclose(f);
        return -1;
    }
    char *buf = malloc(n + 1);
    size_t r = fread(buf, 1, n, f);
    buf[r] = 0;
    fclose(f);
    set_text(st, buf);
    free(buf);
    snprintf(st->path, sizeof st->path, "%s", path);
    st->has_path = 1;
    st->modified = 0;
    update_title(w);
    return 0;
}

static int save_file(Win *w)
{
    St *st = w->st;
    if (!st->has_path) {
        const char *home = getenv("HOME") ? getenv("HOME") : "/root";
        for (int i = 1; i < 1000; i++) {
            snprintf(st->path, sizeof st->path, i == 1 ? "%s/program.axs" : "%s/program%d.axs", home, i);
            if (access(st->path, F_OK) != 0)
                break;
        }
        st->has_path = 1;
    }
    FILE *f = fopen(st->path, "w");
    if (!f) {
        char m[160];
        snprintf(m, sizeof m, "Kaydedilemedi: %s", strerror(errno));
        flash(st, m);
        return -1;
    }
    for (int i = 0; i < st->n; i++) {
        fwrite(st->ln[i].s, 1, st->ln[i].len, f);
        fputc('\n', f);
    }
    fclose(f);
    st->modified = 0;
    char m[PATH_MAX + 32];
    snprintf(m, sizeof m, "Kaydedildi: %s", st->path);
    flash(st, m);
    update_title(w);
    return 0;
}

static Rect out_rect(Win *w)
{
    return (Rect){ 0, w->content.h - STATUS_H - OUT_H, w->content.w, OUT_H };
}

static void out_grid(Win *w, int *cols, int *rows)
{
    int cw, ch;
    term_cell_size(&cw, &ch);
    Rect r = out_rect(w);
    *cols = maxi((r.w - 24) / cw, 10);
    *rows = maxi((r.h - 44) / ch, 3);
}

static void run_program(Win *w)
{
    St *st = w->st;
    if (save_file(w) < 0)
        return;
    if (st->out)
        term_free(st->out);
    int cols, rows;
    out_grid(w, &cols, &rows);
    st->out = term_new(cols, rows);
    char dir[PATH_MAX];
    snprintf(dir, sizeof dir, "%s", st->path);
    char *sl = strrchr(dir, '/');
    if (sl && sl != dir)
        *sl = 0;
    char *argv[] = { "axs", st->path, NULL };
    if (term_spawn(st->out, argv, dir) < 0) {
        flash(st, "axs çalıştırılamadı");
        return;
    }
    const char *b = strrchr(st->path, '/');
    char head[PATH_MAX + 64];
    snprintf(head, sizeof head, "\033[2m$ axs %s\033[0m\r\n", b ? b + 1 : st->path);
    term_feed(st->out, head, (int)strlen(head));
    st->out_open = 1;
    st->out_focus = 1;
    st->out_done = 0;
}

/* ------------------------------------------------------------------ */
/* Sözdizimi renklendirme                                              */
/* ------------------------------------------------------------------ */

static int is_ident_start(unsigned char c) { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }
static int is_ident(unsigned char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

static int is_kw(const char *s, int n, const char **list)
{
    for (int i = 0; list[i]; i++)
        if ((int)strlen(list[i]) == n && !strncmp(list[i], s, n))
            return 1;
    return 0;
}

/* Satırı renk dizisine böl: col[bayt] = renk, bold[bayt] */
static void highlight(const char *s, int n, uint32_t *col, uint8_t *bold)
{
    int i = 0;
    for (int k = 0; k < n; k++) {
        col[k] = T.text;
        bold[k] = 0;
    }
    while (i < n && s[i] == ' ')
        i++;
    /* şablon satırı: ad: metin */
    int j = i;
    while (j < n && is_ident((unsigned char)s[j]))
        j++;
    int k = j;
    while (k < n && s[k] == ' ')
        k++;
    if (j > i && k < n && s[k] == ':' && (k + 1 >= n || s[k + 1] != '=') && !is_kw(s + i, j - i, KEYWORDS)) {
        for (int q = i; q < j; q++) {
            col[q] = C_FN;
            bold[q] = 1;
        }
        col[k] = C_OP;
        int p = k + 1;
        while (p < n) {
            if (s[p] == '(') {
                int depth = 0, q = p;
                for (; q < n; q++) {
                    if (s[q] == '(') depth++;
                    if (s[q] == ')' && --depth == 0) break;
                }
                if (q < n && q + 1 < n && s[q + 1] == ';') {
                    for (int z = p; z <= q + 1; z++)
                        col[z] = C_NUM;
                    p = q + 2;
                    continue;
                }
            }
            if (is_ident_start((unsigned char)s[p]) && (p == k + 1 || !is_ident((unsigned char)s[p - 1]))) {
                int q = p;
                while (q < n && (is_ident((unsigned char)s[q]) || s[q] == '.' || s[q] == '[' || s[q] == ']'))
                    q++;
                if (q < n && s[q] == ';') {
                    for (int z = p; z < q; z++)
                        col[z] = C_VAR;
                    col[q] = ALPHA(C_VAR, 120);
                    p = q + 1;
                    continue;
                }
                for (int z = p; z < q; z++)
                    col[z] = C_TPL;
                p = q;
                continue;
            }
            if (s[p] == '%') {
                int q = p + 1;
                while (q < n && s[q] != '%')
                    q++;
                if (q < n) {
                    for (int z = p; z <= q; z++)
                        col[z] = C_VAR;
                    p = q + 1;
                    continue;
                }
            }
            col[p++] = C_TPL;
        }
        return;
    }
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '#') {
            for (; i < n; i++)
                col[i] = C_COM;
            return;
        }
        if (c == '"' || c == '\'') {
            int q = i + 1;
            while (q < n && s[q] != (char)c) {
                if (s[q] == '\\')
                    q++;
                q++;
            }
            for (int z = i; z <= q && z < n; z++)
                col[z] = C_STR;
            i = q + 1;
            continue;
        }
        if (c >= '0' && c <= '9') {
            while (i < n && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == '_'))
                col[i++] = C_NUM;
            continue;
        }
        if (is_ident_start(c)) {
            int q = i;
            while (q < n && is_ident((unsigned char)s[q]))
                q++;
            uint32_t cc = T.text;
            int b = 0;
            if (is_kw(s + i, q - i, KEYWORDS)) {
                cc = C_KW;
                b = 1;
            } else if (q < n && s[q] == '(') {
                cc = C_FN;
            }
            for (int z = i; z < q; z++) {
                col[z] = cc;
                bold[z] = (uint8_t)b;
            }
            i = q;
            continue;
        }
        if (c == '%') {
            int q = i + 1;
            while (q < n && is_ident((unsigned char)s[q]))
                q++;
            if (q < n && s[q] == '%') {
                for (int z = i; z <= q; z++)
                    col[z] = C_VAR;
                i = q + 1;
                continue;
            }
        }
        if (strchr("=+-*/<>!^.,()[]{}:", c))
            col[i] = C_OP;
        i++;
    }
}

/* ------------------------------------------------------------------ */
/* Çizim                                                               */
/* ------------------------------------------------------------------ */

static Rect editor_rect(Win *w)
{
    St *st = w->st;
    int bottom = w->content.h - STATUS_H - (st->out_open ? OUT_H : 0);
    return (Rect){ 0, TOOL_H, w->content.w, bottom - TOOL_H };
}

static int visible_lines(Win *w) { return maxi(1, editor_rect(w).h / LH); }

static void ensure_visible(Win *w)
{
    St *st = w->st;
    int vis = visible_lines(w);
    if (st->cl < st->top)
        st->top = st->cl;
    if (st->cl >= st->top + vis)
        st->top = st->cl - vis + 1;
    int cw = mono_advance(FS);
    int cols = maxi(10, (w->content.w - GUTTER - 20) / cw);
    int cc = char_col(st->ln[st->cl].s, st->cc);
    if (cc < st->hx)
        st->hx = maxi(0, cc - 8);
    if (cc >= st->hx + cols)
        st->hx = cc - cols + 8;
}

/* Görünür bir editör satırını (arka plan, numara, renkli metin, imleç) çiz. */
static Rect draw_line_row(Win *w, Surf *s, int li, uint32_t *cols, uint8_t *bold)
{
    St *st = w->st;
    Rect er = editor_rect(w);
    int W = s->w;
    int i = li - st->top;
    int y = er.y + 6 + i * LH;
    if (i < 0 || y >= er.y + er.h || li >= st->n)
        return (Rect){ 0, 0, 0, 0 };
    Rect band = { 0, y - 2, W, LH };
    band = rect_isect(band, er);
    Rect oc = s->clip;
    surf_clip(s, rect_isect(oc, band));
    fill_rect(s, band, HEX(0x191926));
    fill_rect(s, (Rect){ 0, band.y, GUTTER, band.h }, HEX(0x161622));
    int cw = mono_advance(FS);
    Line *l = &st->ln[li];
    int cur = li == st->cl;
    if (cur && !st->out_focus)
        fill_rect(s, (Rect){ GUTTER, y - 2, W - GUTTER, LH }, ALPHA(HEX(0xFFFFFF), 8));
    char num[16];
    snprintf(num, sizeof num, "%d", li + 1);
    draw_text(s, cur ? F_MONO_BOLD : F_MONO, 12, GUTTER - 12 - text_width(F_MONO, 12, num), y + 2,
              cur ? T.subtext : HEX(0x45475A), num);
    surf_clip(s, rect_isect(oc, rect_isect(band, (Rect){ GUTTER + 6, er.y, W - GUTTER - 6, er.h })));
    int n = mini(l->len, 4096);
    highlight(l->s, n, cols, bold);
    int c = 0;
    const char *p = l->s;
    while (p < l->s + n) {
        int b = (int)(p - l->s);
        uint32_t cp = utf8_next(&p);
        if (c >= st->hx) {
            int px = GUTTER + 10 + (c - st->hx) * cw;
            if (px > W)
                break;
            if (cp != ' ')
                draw_glyph(s, bold[b] ? F_MONO_BOLD : F_MONO, FS, px, y, cols[b], cp);
        }
        c++;
    }
    /* girinti kılavuzları */
    int ind = 0;
    while (ind < l->len && l->s[ind] == ' ')
        ind++;
    for (int g = 2; g < ind; g += 2)
        if (g >= st->hx)
            fill_rect(s, (Rect){ GUTTER + 10 + (g - st->hx) * cw, y - 2, 1, LH }, ALPHA(HEX(0xFFFFFF), 14));
    if (cur && st->blink_on && !st->out_focus && wm_focused() == w) {
        int ccol = char_col(l->s, st->cc) - st->hx;
        if (ccol >= 0)
            fill_rect(s, (Rect){ GUTTER + 10 + ccol * cw, y - 1, 2, LH - 2 }, T.accent2);
    }
    s->clip = oc;
    return band;
}

static Rect draw_status(Win *w, Surf *s)
{
    St *st = w->st;
    int W = s->w, H = s->h;
    Rect sb = { 0, H - STATUS_H, W, STATUS_H };
    fill_rect(s, sb, T.accent);
    fill_rect(s, sb, ALPHA(HEX(0x000000), 70));
    char info[160];
    snprintf(info, sizeof info, "Satır %d, Sütun %d", st->cl + 1, char_col(st->ln[st->cl].s, st->cc) + 1);
    draw_text(s, F_UI_BOLD, 12, 12, sb.y + 6, HEX(0xFFFFFF), info);
    const char *right = "Axs  •  UTF-8  •  F5 çalıştır  •  Ctrl+S kaydet";
    draw_text(s, F_UI, 12, W - 12 - text_width(F_UI, 12, right), sb.y + 6, ALPHA(HEX(0xFFFFFF), 220), right);
    if (st->msg[0] && now_ms() < st->msg_until)
        draw_text_fit(s, F_UI, 12, 150, sb.y + 6, W - 150 - text_width(F_UI, 12, right) - 30, HEX(0xFFFFFF), st->msg);
    return sb;
}

static void st_draw(Win *w, Surf *s)
{
    St *st = w->st;
    if (st->fast_ok) {
        /* hızlı yol: yazarken/imleç hareketinde yalnızca etkilenen satırlar */
        st->fast_ok = 0;
        uint32_t cols[4096];
        uint8_t bold[4096];
        Rect d = draw_status(w, s);
        for (int k = 0; k < st->fast_n; k++) {
            Rect band = draw_line_row(w, s, st->fast_lines[k], cols, bold);
            if (rect_empty(band))
                continue;
            /* satır şeridinin üstünden geçen kaydırma çubuğu parçasını geri çiz */
            Rect er = editor_rect(w);
            int vis = er.h / LH;
            Rect oc = s->clip;
            surf_clip(s, rect_isect(oc, band));
            ui_scrollbar(s, (Rect){ s->w - 8, er.y + 4, 5, er.h - 8 }, st->n + vis / 2, vis, st->top);
            s->clip = oc;
            d = rect_union(d, band);
        }
        w->dmg = d;
        return;
    }
    UiState *u = &st->ui;
    ui_begin(u);
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, HEX(0x191926));

    /* araç çubuğu */
    fill_rect(s, (Rect){ 0, 0, W, TOOL_H }, T.surface);
    fill_rect(s, (Rect){ 0, TOOL_H - 1, W, 1 }, T.border);
    int x = 12;
    if (ui_button(s, u, (Rect){ x, 9, 70, 32 }, "Yeni", BTN_NORMAL)) {
        set_text(st, "# Yeni Axs programı\n\n");
        st->has_path = 0;
        st->modified = 0;
        st->cl = 2;
        update_title(w);
    }
    x += 78;
    if (ui_button(s, u, (Rect){ x, 9, 92, 32 }, "Örnekler", BTN_NORMAL))
        wm_open(&APP_FILES, "/usr/lib/axs/examples");
    x += 100;
    if (ui_button(s, u, (Rect){ x, 9, 84, 32 }, "Kaydet", BTN_NORMAL))
        save_file(w);
    x += 92;
    int running = st->out && term_alive(st->out);
    Rect rb = { x, 9, 118, 32 };
    if (running) {
        if (ui_button(s, u, rb, "■  Durdur", BTN_DANGER))
            term_kill(st->out);
    } else {
        if (ui_button(s, u, rb, "", BTN_PRIMARY))
            run_program(w);
        draw_icon(s, IC_PLAY, rb.x + 14, rb.y + 8, 16);
        draw_text(s, F_UI_BOLD, 13, rb.x + 38, rb.y + 8, HEX(0xFFFFFF), "Çalıştır");
    }
    const char *fname = st->has_path ? strrchr(st->path, '/') + 1 : "adsız.axs";
    int fw = text_width(F_UI_BOLD, 13, fname);
    int fx = W - fw - 24;
    draw_icon(s, IC_AXSFILE, fx - 26, 15, 20);
    draw_text(s, F_UI_BOLD, 13, fx, 17, T.text, fname);
    if (st->modified)
        fill_circle(s, fx + fw + 10.f, 25.f, 3.5f, T.accent2);

    /* editör */
    Rect er = editor_rect(w);
    fill_rect(s, (Rect){ 0, er.y, GUTTER, er.h }, HEX(0x161622));
    int vis = er.h / LH;
    uint32_t *cols = malloc(sizeof(uint32_t) * 4096);
    uint8_t *bold = malloc(4096);
    for (int i = 0; i < vis + 1 && st->top + i < st->n; i++)
        draw_line_row(w, s, st->top + i, cols, bold);
    free(cols);
    free(bold);
    ui_scrollbar(s, (Rect){ W - 8, er.y + 4, 5, er.h - 8 }, st->n + vis / 2, vis, st->top);

    /* çıktı paneli */
    if (st->out_open && st->out) {
        Rect orr = out_rect(w);
        fill_rect(s, orr, HEX(0x13131D));
        fill_rect(s, (Rect){ 0, orr.y, W, 1 }, st->out_focus ? T.accent : T.border);
        draw_icon(s, IC_TERMINAL, 12, orr.y + 8, 18);
        draw_text(s, F_UI_BOLD, 12, 38, orr.y + 10, T.text, "Çıktı");
        const char *state = term_alive(st->out) ? "çalışıyor…" : NULL;
        char done[48];
        if (!state) {
            snprintf(done, sizeof done, term_exit_code(st->out) ? "hata ile bitti (kod %d)" : "bitti", term_exit_code(st->out));
            state = done;
        }
        ui_badge(s, 84, orr.y + 8, state, term_alive(st->out) ? ALPHA(T.accent, 90) : term_exit_code(st->out) ? ALPHA(T.red, 80) : ALPHA(T.green, 70), T.text);
        Rect xb = { W - 38, orr.y + 5, 28, 26 };
        if (ui_hover(u, xb))
            fill_rrect(s, xb, 7, T.overlay);
        draw_line(s, xb.x + 9.f, xb.y + 8.f, xb.x + 19.f, xb.y + 18.f, 1.8f, T.subtext);
        draw_line(s, xb.x + 19.f, xb.y + 8.f, xb.x + 9.f, xb.y + 18.f, 1.8f, T.subtext);
        if (ui_clicked(u, xb)) {
            if (term_alive(st->out))
                term_kill(st->out);
            st->out_open = 0;
            st->out_focus = 0;
        }
        int cws, chs;
        term_cell_size(&cws, &chs);
        int oc2, orow;
        out_grid(w, &oc2, &orow);
        term_draw(st->out, s, (Rect){ orr.x + 12, orr.y + 36, oc2 * cws, orow * chs }, st->out_focus && wm_focused() == w, 1);
    }

    draw_status(w, s);
}

/* ------------------------------------------------------------------ */
/* Düzenleme                                                           */
/* ------------------------------------------------------------------ */

static void edited(Win *w)
{
    St *st = w->st;
    if (!st->modified) {
        st->modified = 1;
        update_title(w);
    }
    st->blink_on = 1;
    st->blink_t = 0;
}

static void insert_text(Win *w, const char *s, int n)
{
    St *st = w->st;
    line_insert(&st->ln[st->cl], st->cc, s, n);
    st->cc += n;
    edited(w);
}

static void newline(Win *w)
{
    St *st = w->st;
    Line *l = &st->ln[st->cl];
    int ind = 0;
    while (ind < l->len && l->s[ind] == ' ' && ind < st->cc)
        ind++;
    /* blok başlatan satırdan sonra bir kademe içeri */
    int f = ind;
    while (f < l->len && is_ident((unsigned char)l->s[f]))
        f++;
    int extra = is_kw(l->s + ind, f - ind, BLOCK_START) && st->cc == l->len ? 2 : 0;
    char *rest = strdup(l->s + st->cc);
    int restn = l->len - st->cc;
    l->len = st->cc;
    l->s[l->len] = 0;
    lines_insert(w->st, st->cl + 1);
    st->cl++;
    Line *nl = &st->ln[st->cl];
    for (int i = 0; i < ind + extra; i++)
        line_insert(nl, nl->len, " ", 1);
    line_insert(nl, nl->len, rest, restn);
    free(rest);
    st->cc = ind + extra;
    edited(w);
}

static void backspace(Win *w)
{
    St *st = w->st;
    Line *l = &st->ln[st->cl];
    if (st->cc > 0) {
        int only_spaces = 1;
        for (int i = 0; i < st->cc; i++)
            if (l->s[i] != ' ')
                only_spaces = 0;
        if (only_spaces && st->cc >= 2 && st->cc % 2 == 0) {
            line_delete(l, st->cc - 2, 2);
            st->cc -= 2;
        } else {
            int p = utf8_prev(l->s, st->cc);
            line_delete(l, p, st->cc - p);
            st->cc = p;
        }
    } else if (st->cl > 0) {
        Line *pl = &st->ln[st->cl - 1];
        int at = pl->len;
        line_insert(pl, pl->len, l->s, l->len);
        lines_remove(st, st->cl);
        st->cl--;
        st->cc = at;
    }
    edited(w);
}

static void del_forward(Win *w)
{
    St *st = w->st;
    Line *l = &st->ln[st->cl];
    if (st->cc < l->len) {
        const char *q = l->s + st->cc;
        utf8_next(&q);
        line_delete(l, st->cc, (int)(q - (l->s + st->cc)));
    } else if (st->cl + 1 < st->n) {
        Line *nx = &st->ln[st->cl + 1];
        line_insert(l, l->len, nx->s, nx->len);
        lines_remove(st, st->cl + 1);
    }
    edited(w);
}

static void toggle_comment(Win *w)
{
    St *st = w->st;
    Line *l = &st->ln[st->cl];
    int i = 0;
    while (i < l->len && l->s[i] == ' ')
        i++;
    if (i < l->len && l->s[i] == '#') {
        int n = (i + 1 < l->len && l->s[i + 1] == ' ') ? 2 : 1;
        line_delete(l, i, n);
        st->cc = maxi(i, st->cc - n);
    } else {
        line_insert(l, i, "# ", 2);
        st->cc += 2;
    }
    edited(w);
}

static void st_key_inner(Win *w, KeyEv *e);

static void st_key(Win *w, KeyEv *e)
{
    St *st = w->st;
    if (!e->down)
        return;
    int o_top = st->top, o_hx = st->hx, o_n = st->n, o_cl = st->cl, o_mod = st->modified, o_out = st->out_focus;
    st_key_inner(w, e);
    if (w->dirty && st->top == o_top && st->hx == o_hx && st->n == o_n && st->modified == o_mod &&
        st->out_focus == o_out && !o_out) {
        st->fast_ok = 1;
        st->fast_n = 0;
        st->fast_lines[st->fast_n++] = o_cl;
        if (st->cl != o_cl)
            st->fast_lines[st->fast_n++] = st->cl;
    } else {
        st->fast_ok = 0;
    }
}

static void st_key_inner(Win *w, KeyEv *e)
{
    St *st = w->st;
    int ctrl = e->mods & MOD_CTRL;
    if ((ctrl && e->code == KEY_R) || e->code == KEY_F5) {
        if (!(st->out && term_alive(st->out)))
            run_program(w);
        w->dirty = 1;
        return;
    }
    if (ctrl && e->code == KEY_S) {
        save_file(w);
        w->dirty = 1;
        return;
    }
    if (st->out_focus && st->out) {
        if (e->code == KEY_ESC && !term_alive(st->out)) {
            st->out_focus = 0;
        } else if (term_alive(st->out)) {
            term_key(st->out, e);
        } else {
            st->out_focus = 0; /* program bitti: yazmaya editörde devam et */
            st_key_inner(w, e);
            return;
        }
        w->dirty = 1;
        return;
    }
    Line *l = &st->ln[st->cl];
    int vis = visible_lines(w);
    int keep_want = 0;
    switch (e->code) {
    case KEY_LEFT:
        if (st->cc > 0)
            st->cc = utf8_prev(l->s, st->cc);
        else if (st->cl > 0) {
            st->cl--;
            st->cc = st->ln[st->cl].len;
        }
        break;
    case KEY_RIGHT:
        if (st->cc < l->len) {
            const char *q = l->s + st->cc;
            utf8_next(&q);
            st->cc = (int)(q - l->s);
        } else if (st->cl + 1 < st->n) {
            st->cl++;
            st->cc = 0;
        }
        break;
    case KEY_UP:
    case KEY_DOWN:
    case KEY_PAGEUP:
    case KEY_PAGEDOWN: {
        int d = e->code == KEY_UP ? -1 : e->code == KEY_DOWN ? 1 : e->code == KEY_PAGEUP ? -vis : vis;
        if (st->want < 0)
            st->want = char_col(l->s, st->cc);
        st->cl = clampi(st->cl + d, 0, st->n - 1);
        st->cc = col_byte(&st->ln[st->cl], st->want);
        keep_want = 1;
        break;
    }
    case KEY_HOME: {
        int i = 0;
        while (i < l->len && l->s[i] == ' ')
            i++;
        st->cc = st->cc == i ? 0 : i;
        if (ctrl)
            st->cl = st->cc = 0;
        break;
    }
    case KEY_END:
        if (ctrl)
            st->cl = st->n - 1;
        st->cc = st->ln[st->cl].len;
        break;
    case KEY_ENTER:
    case KEY_KPENTER:
        newline(w);
        break;
    case KEY_BACKSPACE:
        backspace(w);
        break;
    case KEY_DELETE:
        del_forward(w);
        break;
    case KEY_TAB:
        if (e->mods & MOD_SHIFT) {
            int n = 0;
            while (n < 2 && n < l->len && l->s[n] == ' ')
                n++;
            line_delete(l, 0, n);
            st->cc = maxi(0, st->cc - n);
            edited(w);
        } else {
            insert_text(w, "  ", 2);
        }
        break;
    default:
        if (ctrl && e->code == KEY_D) {
            lines_insert(st, st->cl + 1);
            line_set(&st->ln[st->cl + 1], st->ln[st->cl].s, st->ln[st->cl].len);
            st->cl++;
            edited(w);
        } else if (ctrl && (e->code == KEY_SLASH || e->code == KEY_7 || e->code == KEY_KPSLASH)) {
            toggle_comment(w);
        } else if (ctrl && e->code == KEY_N) {
            set_text(st, "# Yeni Axs programı\n\n");
            st->has_path = 0;
            st->modified = 0;
            update_title(w);
        } else if (!ctrl && e->ch >= 32) {
            char b[4];
            int n = utf8_put(b, e->ch);
            insert_text(w, b, n);
        } else {
            return;
        }
    }
    if (!keep_want)
        st->want = -1;
    st->blink_on = 1;
    st->blink_t = 0;
    ensure_visible(w);
    w->dirty = 1;
}

static void st_mouse(Win *w, MouseEv *e)
{
    St *st = w->st;
    st->fast_ok = 0;
    Rect er = editor_rect(w);
    if (e->kind == M_WHEEL) {
        if (st->out_open && rect_has(out_rect(w), e->x, e->y) && st->out) {
            term_scroll(st->out, e->wheel * 3);
        } else {
            st->top = clampi(st->top - e->wheel * 3, 0, maxi(0, st->n - visible_lines(w) / 2));
        }
        w->dirty = 1;
        return;
    }
    if (e->kind == M_DOWN && e->button == 1) {
        if (rect_has(er, e->x, e->y)) {
            st->out_focus = 0;
            int li = clampi(st->top + (e->y - er.y - 6) / LH, 0, st->n - 1);
            int cw = mono_advance(FS);
            int col = maxi(0, (e->x - GUTTER - 10 + cw / 2) / cw + st->hx);
            st->cl = li;
            st->cc = col_byte(&st->ln[li], col);
            st->want = -1;
            st->blink_on = 1;
            w->dirty = 1;
        } else if (st->out_open && rect_has(out_rect(w), e->x, e->y)) {
            st->out_focus = 1;
            w->dirty = 1;
        }
    }
    ui_dispatch(w, &st->ui, e);
}

static int st_fds(Win *w, int *fds, int max)
{
    St *st = w->st;
    if (st->out && term_fd(st->out) >= 0 && max > 0) {
        fds[0] = term_fd(st->out);
        return 1;
    }
    return 0;
}

static void st_io(Win *w, int fd)
{
    (void)fd;
    St *st = w->st;
    st->fast_ok = 0;
    if (!term_read(st->out)) {
        int code = term_exit_code(st->out);
        char m[96];
        snprintf(m, sizeof m, "\r\n\033[%sm── program %s (kod %d) ──\033[0m\r\n", code ? "31" : "32", code ? "hata ile bitti" : "bitti", code);
        term_feed(st->out, m, (int)strlen(m));
    }
    w->dirty = 1;
}

static void st_tick(Win *w)
{
    St *st = w->st;
    if (wm_focused() != w)
        return;
    if (++st->blink_t >= 2) {
        st->blink_t = 0;
        st->blink_on = !st->blink_on;
        if (!w->dirty && !st->out_focus) {
            st->fast_ok = 1;
            st->fast_n = 1;
            st->fast_lines[0] = st->cl;
        }
        w->dirty = 1;
    }
    if (st->msg[0] && now_ms() >= st->msg_until) {
        st->msg[0] = 0;
        w->dirty = 1;
    }
}

static void st_resize(Win *w)
{
    St *st = w->st;
    st->fast_ok = 0;
    if (st->out) {
        int c, r;
        out_grid(w, &c, &r);
        term_resize(st->out, c, r);
    }
}

static void st_init(Win *w, const char *arg)
{
    St *st = calloc(1, sizeof *st);
    st->ui.cur_hot = -1;
    st->want = -1;
    st->blink_on = 1;
    w->st = st;
    w->min_w = 520;
    w->min_h = 360;
    if (!arg || load_file(w, arg) < 0) {
        set_text(st, SAMPLE);
        if (arg)
            flash(st, "Dosya açılamadı, örnek program yüklendi");
        update_title(w);
    }
}

static void st_close(Win *w)
{
    St *st = w->st;
    if (st->out)
        term_free(st->out);
    for (int i = 0; i < st->n; i++)
        free(st->ln[i].s);
    free(st->ln);
    free(st);
}

const App APP_STUDIO = {
    .id = "studyo", .name = "Axs Stüdyo", .desc = "Axs kod editörü ve çalıştırıcı",
    .icon = IC_STUDIO, .w = 860, .h = 560,
    .init = st_init, .draw = st_draw, .key = st_key, .mouse = st_mouse,
    .fds = st_fds, .io = st_io, .tick = st_tick, .resize = st_resize, .close = st_close,
};
