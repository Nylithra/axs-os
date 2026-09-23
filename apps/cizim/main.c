/* Çizim - AxsOS resim programı
 * Fırça, silgi, çizgi, dikdörtgen, elips, boya kovası; 12 renk; geri al (Ctrl+Z);
 * PNG olarak ~/Resimler klasörüne kaydeder.
 */
#include "axsapp.h"

#include <errno.h>
#include <linux/input.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8 0
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

#define BAR 60
#define STATUS 28
#define CW 1280
#define CH 800
#define UNDO 6

enum { T_BRUSH, T_ERASER, T_LINE, T_RECT, T_ELLIPSE, T_FILL, T_COUNT };
static const char *TOOL_NAMES[T_COUNT] = { "Fırça", "Silgi", "Çizgi", "Kare", "Elips", "Kova" };
static const uint32_t PALETTE[12] = {
    0xFF111111, 0xFF6B7280, 0xFFFFFFFF, 0xFFEF4444, 0xFFF97316, 0xFFFACC15,
    0xFF22C55E, 0xFF14B8A6, 0xFF3B82F6, 0xFF8B5CF6, 0xFFEC4899, 0xFF92400E,
};
static const int SIZES[4] = { 2, 5, 10, 20 };

static Surf canvas;
static uint32_t *undo_buf[UNDO];
static int undo_n;
static int tool = T_BRUSH, color_i = 0, size_i = 1;
static int drawing, lx, ly, sx, sy; /* sürükleme: son ve başlangıç (tuval koordinatı) */
static Rect last_preview;
static char status[200] = "Hazır — tuvale çizmeye başla";

static Rect canvas_view(Surf *s) { return (Rect){ 0, BAR, s->w, s->h - BAR - STATUS }; }

static uint32_t cur_color(void) { return tool == T_ERASER ? 0xFFFFFFFF : PALETTE[color_i]; }
static int cur_size(void) { return SIZES[size_i] * (tool == T_ERASER ? 2 : 1); }

static void push_undo(void)
{
    if (undo_n == UNDO) {
        free(undo_buf[0]);
        memmove(undo_buf, undo_buf + 1, sizeof(undo_buf[0]) * (UNDO - 1));
        undo_n--;
    }
    uint32_t *b = malloc((size_t)CW * CH * 4);
    if (!b)
        return;
    memcpy(b, canvas.px, (size_t)CW * CH * 4);
    undo_buf[undo_n++] = b;
}

static void undo(void)
{
    if (!undo_n) {
        snprintf(status, sizeof status, "Geri alınacak bir şey yok");
        return;
    }
    memcpy(canvas.px, undo_buf[undo_n - 1], (size_t)CW * CH * 4);
    free(undo_buf[--undo_n]);
    snprintf(status, sizeof status, "Geri alındı");
}

static void clear_canvas(void)
{
    push_undo();
    surf_noclip(&canvas);
    fill_rect(&canvas, (Rect){ 0, 0, CW, CH }, 0xFFFFFFFF);
}

/* Kenar yumuşatmalı kalın çizgi + yuvarlak uçlar */
static void stroke(Surf *d, int x0, int y0, int x1, int y1, int w, uint32_t c)
{
    if (x0 != x1 || y0 != y1)
        draw_line(d, x0 + .5f, y0 + .5f, x1 + .5f, y1 + .5f, (float)w, c);
    if (w > 2) {
        fill_circle(d, x0 + .5f, y0 + .5f, w / 2.f, c);
        fill_circle(d, x1 + .5f, y1 + .5f, w / 2.f, c);
    }
}

static void shape(Surf *d, int t, int x0, int y0, int x1, int y1, int w, uint32_t c)
{
    if (t == T_LINE) {
        stroke(d, x0, y0, x1, y1, w, c);
    } else if (t == T_RECT) {
        stroke(d, x0, y0, x1, y0, w, c);
        stroke(d, x1, y0, x1, y1, w, c);
        stroke(d, x1, y1, x0, y1, w, c);
        stroke(d, x0, y1, x0, y0, w, c);
    } else if (t == T_ELLIPSE) {
        float cx = (x0 + x1) / 2.f, cy = (y0 + y1) / 2.f, rx = (float)abs(x1 - x0) / 2.f, ry = (float)abs(y1 - y0) / 2.f;
        int n = 72;
        float px = cx + rx, py = cy;
        for (int i = 1; i <= n; i++) {
            float a = i * 6.2831853f / n, nx = cx + rx * cosf(a), ny = cy + ry * sinf(a);
            draw_line(d, px, py, nx, ny, (float)w, c);
            if (w > 3)
                fill_circle(d, nx, ny, w / 2.f, c);
            px = nx, py = ny;
        }
    }
}

static Rect seg_rect(int x0, int y0, int x1, int y1, int w)
{
    int pad = w / 2 + 3;
    return (Rect){ mini(x0, x1) - pad, mini(y0, y1) - pad, abs(x1 - x0) + 2 * pad + 1, abs(y1 - y0) + 2 * pad + 1 };
}

/* Taşma dolgusu (yığıtlı tarama satırı) */
static void flood(int x, int y, uint32_t nc)
{
    uint32_t oc = canvas.px[y * CW + x];
    if (oc == nc)
        return;
    int cap = CW * CH / 2, n = 0;
    int *st = malloc(sizeof(int) * 2 * cap);
    if (!st)
        return;
    st[n++] = x, st[n++] = y;
    while (n) {
        int py = st[--n], px = st[--n];
        uint32_t *row = &canvas.px[py * CW];
        int l = px, r = px;
        while (l > 0 && row[l - 1] == oc) l--;
        while (r < CW - 1 && row[r + 1] == oc) r++;
        for (int i = l; i <= r; i++)
            row[i] = nc;
        for (int dy = -1; dy <= 1; dy += 2) {
            int ny = py + dy;
            if (ny < 0 || ny >= CH)
                continue;
            uint32_t *nr = &canvas.px[ny * CW];
            for (int i = l; i <= r; i++)
                if (nr[i] == oc && (i == l || nr[i - 1] != oc) && n < 2 * cap - 2)
                    st[n++] = i, st[n++] = ny;
        }
    }
    free(st);
}

static void save_png(void)
{
    const char *home = getenv("HOME") ? getenv("HOME") : "/root";
    char dir[256], path[320];
    snprintf(dir, sizeof dir, "%s/Resimler", home);
    mkdir(dir, 0755);
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(path, sizeof path, "%s/cizim-%04d%02d%02d-%02d%02d%02d.png", dir, tm.tm_year + 1900, tm.tm_mon + 1,
             tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    /* görünen tuval boyutunda kaydet */
    Surf *s = axsapp_surf();
    Rect v = canvas_view(s);
    int w = mini(v.w, CW), h = mini(v.h, CH);
    unsigned char *rgba = malloc((size_t)w * h * 4);
    if (!rgba)
        return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t p = canvas.px[y * CW + x];
            unsigned char *o = &rgba[(y * w + x) * 4];
            o[0] = (p >> 16) & 255, o[1] = (p >> 8) & 255, o[2] = p & 255, o[3] = 255;
        }
    if (stbi_write_png(path, w, h, 4, rgba, w * 4))
        snprintf(status, sizeof status, "Kaydedildi: %s", path);
    else
        snprintf(status, sizeof status, "Kaydedilemedi: %s (%s)", path, strerror(errno));
    free(rgba);
}

static void draw(Surf *s, UiState *u)
{
    int W = s->w, H = s->h;
    Rect v = canvas_view(s);
    /* tuval */
    fill_rect(s, v, HEX(0x2A2A34));
    Rect cr = { v.x, v.y, mini(v.w, CW), mini(v.h, CH) };
    blit(s, cr.x, cr.y, &canvas, (Rect){ 0, 0, cr.w, cr.h });
    if (drawing && tool >= T_LINE && tool <= T_ELLIPSE) {
        Rect clip = s->clip;
        surf_clip(s, rect_isect(clip, cr));
        shape(s, tool, v.x + sx, v.y + sy, v.x + lx, v.y + ly, cur_size(), cur_color());
        surf_clip(s, clip);
    }
    /* araç çubuğu */
    Rect bar = { 0, 0, W, BAR };
    fill_rect(s, bar, T.surface);
    fill_rect(s, (Rect){ 0, BAR - 1, W, 1 }, T.border);
    int x = 10;
    for (int i = 0; i < T_COUNT; i++) {
        Rect b = { x, 12, 54, 36 };
        if (ui_button(s, u, b, TOOL_NAMES[i], i == tool ? BTN_PRIMARY : BTN_GHOST))
            tool = i;
        x += 58;
    }
    x += 8;
    fill_rect(s, (Rect){ x, 14, 1, 32 }, T.border);
    x += 10;
    for (int i = 0; i < 4; i++) {
        Rect b = { x, 12, 32, 36 };
        int hov = ui_hover(u, b);
        if (i == size_i || hov)
            fill_rrect(s, b, 8, i == size_i ? ALPHA(T.accent, 90) : T.surface2);
        fill_circle(s, b.x + 16.f, b.y + 18.f, mini(SIZES[i], 16) / 2.f + 1, T.text);
        if (ui_clicked(u, b))
            size_i = i;
        x += 34;
    }
    x += 8;
    fill_rect(s, (Rect){ x, 14, 1, 32 }, T.border);
    x += 12;
    /* renk paleti (2 satır x 6) */
    for (int i = 0; i < 12; i++) {
        Rect b = { x + (i % 6) * 22, 10 + (i / 6) * 21, 18, 18 };
        fill_rrect(s, b, 5, PALETTE[i]);
        stroke_rrect(s, b, 5, 1, ALPHA(HEX(0xFFFFFF), 40));
        if (i == color_i)
            stroke_rrect(s, rect_inset(b, -3), 7, 2, T.accent);
        if (ui_clicked(u, b)) {
            color_i = i;
            if (tool == T_ERASER)
                tool = T_BRUSH;
        }
    }
    x += 6 * 22 + 12;
    if (ui_button(s, u, (Rect){ W - 228, 12, 64, 36 }, "Geri", BTN_NORMAL))
        undo();
    if (ui_button(s, u, (Rect){ W - 158, 12, 72, 36 }, "Temizle", BTN_NORMAL)) {
        clear_canvas();
        snprintf(status, sizeof status, "Tuval temizlendi (Ctrl+Z ile geri al)");
    }
    if (ui_button(s, u, (Rect){ W - 80, 12, 70, 36 }, "Kaydet", BTN_PRIMARY))
        save_png();
    /* durum çubuğu */
    Rect st = { 0, H - STATUS, W, STATUS };
    fill_rect(s, st, T.base);
    fill_rect(s, (Rect){ 0, st.y, W, 1 }, T.border);
    fill_rrect(s, (Rect){ 10, st.y + 7, 14, 14 }, 4, cur_color());
    char info[260];
    snprintf(info, sizeof info, "%s  •  %d px  •  %s", TOOL_NAMES[tool], cur_size(), status);
    draw_text_fit(s, F_UI, 12, 32, st.y + 7, W - 140, T.subtext, info);
    draw_text(s, F_UI, 12, W - 110, st.y + 7, T.muted, "Ctrl+Z: geri al");
}

static void mouse(MouseEv *e)
{
    Surf *s = axsapp_surf();
    Rect v = canvas_view(s);
    Rect cr = { v.x, v.y, mini(v.w, CW), mini(v.h, CH) };
    int cx = clampi(e->x - v.x, 0, CW - 1), cy = clampi(e->y - v.y, 0, CH - 1);
    if (e->kind == M_DOWN && e->button == 1 && rect_has(cr, e->x, e->y)) {
        push_undo();
        if (tool == T_FILL) {
            flood(cx, cy, cur_color());
            axsapp_redraw_rect(cr);
            return;
        }
        drawing = 1;
        lx = sx = cx, ly = sy = cy;
        if (tool == T_BRUSH || tool == T_ERASER) {
            surf_noclip(&canvas);
            stroke(&canvas, cx, cy, cx, cy, cur_size(), cur_color());
            Rect r = seg_rect(cx, cy, cx, cy, cur_size());
            axsapp_redraw_rect((Rect){ r.x + v.x, r.y + v.y, r.w, r.h });
        }
        last_preview = (Rect){ 0, 0, 0, 0 };
    } else if (e->kind == M_MOVE && drawing) {
        if (tool == T_BRUSH || tool == T_ERASER) {
            surf_noclip(&canvas);
            stroke(&canvas, lx, ly, cx, cy, cur_size(), cur_color());
            Rect r = seg_rect(lx, ly, cx, cy, cur_size());
            axsapp_redraw_rect((Rect){ r.x + v.x, r.y + v.y, r.w, r.h });
        } else {
            Rect r = seg_rect(sx, sy, cx, cy, cur_size());
            r = (Rect){ r.x + v.x, r.y + v.y, r.w, r.h };
            axsapp_redraw_rect(rect_empty(last_preview) ? r : rect_union(r, last_preview));
            last_preview = r;
        }
        lx = cx, ly = cy;
    } else if (e->kind == M_UP && drawing) {
        drawing = 0;
        if (tool >= T_LINE && tool <= T_ELLIPSE) {
            surf_noclip(&canvas);
            shape(&canvas, tool, sx, sy, cx, cy, cur_size(), cur_color());
        }
        axsapp_redraw();
    } else if (e->kind == M_WHEEL) {
        size_i = clampi(size_i + (e->wheel > 0 ? 1 : -1), 0, 3);
        axsapp_redraw();
    }
}

static void key(KeyEv *e)
{
    if (!e->down)
        return;
    if ((e->mods & MOD_CTRL) && e->code == KEY_Z)
        undo();
    else if ((e->mods & MOD_CTRL) && e->code == KEY_S)
        save_png();
    else if (e->code == KEY_B) tool = T_BRUSH;
    else if (e->code == KEY_E) tool = T_ERASER;
    else if (e->code == KEY_L) tool = T_LINE;
    else if (e->code == KEY_R) tool = T_RECT;
    else if (e->code == KEY_O) tool = T_ELLIPSE;
    else if (e->code == KEY_F) tool = T_FILL;
    else if (e->code >= KEY_1 && e->code <= KEY_4) size_i = e->code - KEY_1;
}

int main(void)
{
    canvas = surf_new(CW, CH);
    fill_rect(&canvas, (Rect){ 0, 0, CW, CH }, 0xFFFFFFFF);
    AxsAppFuncs f = { .draw = draw, .mouse = mouse, .key = key };
    return axsapp_run("cizim", "Çizim", 920, 640, &f);
}
