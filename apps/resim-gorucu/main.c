/* Resim Görüntüleyici - AxsOS
 * PNG, JPG, BMP, GIF. Aynı klasördeki resimler arasında ←/→ ile gezinme, tekerlekle yakınlaştırma.
 */
#include "axsapp.h"

#include <dirent.h>
#include <limits.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define SAMPLES "/usr/share/axsde/ornek-resimler"
#define BAR 52

static char dir[PATH_MAX];
static char files[256][256];
static int nfiles, cur = -1;
static Surf img;
static int have_img;
static float zoom = 0; /* 0 = pencereye sığdır */
static char err[160];
static long long fsize;

static int is_image(const char *n)
{
    const char *d = strrchr(n, '.');
    return d && (!strcasecmp(d, ".png") || !strcasecmp(d, ".jpg") || !strcasecmp(d, ".jpeg") ||
                 !strcasecmp(d, ".bmp") || !strcasecmp(d, ".gif"));
}

static int cmp(const void *a, const void *b) { return strcasecmp(a, b); }

static void scan(const char *d)
{
    snprintf(dir, sizeof dir, "%s", d);
    nfiles = 0;
    DIR *dp = opendir(d);
    struct dirent *e;
    while (dp && (e = readdir(dp)) && nfiles < 256)
        if (e->d_name[0] != '.' && is_image(e->d_name))
            snprintf(files[nfiles++], 256, "%s", e->d_name);
    if (dp)
        closedir(dp);
    qsort(files, nfiles, 256, cmp);
}

static void load(int i)
{
    if (have_img)
        surf_free(&img);
    have_img = 0;
    err[0] = 0;
    if (i < 0 || i >= nfiles)
        return;
    cur = i;
    char p[PATH_MAX + 256];
    snprintf(p, sizeof p, "%s/%s", dir, files[i]);
    struct stat st;
    fsize = stat(p, &st) == 0 ? st.st_size : 0;
    if (img_load(p, &img) == 0)
        have_img = 1;
    else
        snprintf(err, sizeof err, "Açılamadı: %s", files[i]);
    zoom = 0;
    char t[300];
    snprintf(t, sizeof t, "%s — Resim Görüntüleyici", files[i]);
    axsapp_title(t);
}

/* İkili doğrusal ölçekleme ile resmi hedef dikdörtgene çiz (dama arka plan saydamlık için) */
static void draw_scaled(Surf *s, Rect dst, Rect clip)
{
    Rect k = rect_isect(dst, clip);
    if (rect_empty(k))
        return;
    float sx = (float)img.w / dst.w, sy = (float)img.h / dst.h;
    for (int y = k.y; y < k.y + k.h; y++) {
        float fy = (y - dst.y + 0.5f) * sy - 0.5f;
        int y0 = (int)fy;
        if (fy < 0) y0 = 0, fy = 0;
        int y1 = mini(y0 + 1, img.h - 1);
        int wy = (int)((fy - y0) * 256);
        uint32_t *row = &s->px[y * s->stride];
        for (int x = k.x; x < k.x + k.w; x++) {
            float fx = (x - dst.x + 0.5f) * sx - 0.5f;
            int x0 = (int)fx;
            if (fx < 0) x0 = 0, fx = 0;
            int x1 = mini(x0 + 1, img.w - 1);
            int wx = (int)((fx - x0) * 256);
            uint32_t p00 = img.px[y0 * img.stride + x0], p01 = img.px[y0 * img.stride + x1];
            uint32_t p10 = img.px[y1 * img.stride + x0], p11 = img.px[y1 * img.stride + x1];
            uint32_t out = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                int a = (int)((p00 >> sh) & 255) * (256 - wx) + (int)((p01 >> sh) & 255) * wx;
                int b = (int)((p10 >> sh) & 255) * (256 - wx) + (int)((p11 >> sh) & 255) * wx;
                out |= (uint32_t)(((a * (256 - wy) + b * wy) >> 16) & 255) << sh;
            }
            uint32_t al = out >> 24;
            if (al < 255) { /* saydam alan: dama deseni üzerine karıştır */
                uint32_t bg = (((x >> 3) + (y >> 3)) & 1) ? 0xFF3A3A48 : 0xFF2A2A36;
                out = mix(bg, out | 0xFF000000u, (int)al);
            }
            row[x] = out | 0xFF000000u;
        }
    }
}

static void draw(Surf *s, UiState *u)
{
    int W = s->w, H = s->h;
    fill_rect(s, (Rect){ 0, 0, W, H }, HEX(0x101016));
    Rect view = { 0, 0, W, H - BAR };
    if (have_img) {
        float fit = mini(view.w - 24, img.w) / (float)img.w;
        float fh = mini(view.h - 24, img.h) / (float)img.h;
        if (fh < fit)
            fit = fh;
        float z = zoom > 0 ? zoom : fit;
        int dw = maxi(1, (int)(img.w * z)), dh = maxi(1, (int)(img.h * z));
        Rect d = { (view.w - dw) / 2, (view.h - dh) / 2, dw, dh };
        draw_scaled(s, d, view);
    } else {
        Rect c = { W / 2 - 180, view.h / 2 - 70, 360, 140 };
        fill_rrect(s, c, 18, T.surface2);
        draw_text_center(s, F_UI_BOLD, 18, (Rect){ c.x, c.y + 24, c.w, 26 }, T.text,
                         err[0] ? "Resim açılamadı" : nfiles ? "Resim seç" : "Bu klasörde resim yok");
        draw_text_center(s, F_UI, 13, (Rect){ c.x, c.y + 56, c.w, 20 }, T.subtext,
                         err[0] ? err : "Dosyalar'dan bir resme çift tıkla");
        if (ui_button(s, u, (Rect){ c.x + 90, c.y + 88, 180, 36 }, "Örnek resimleri aç", BTN_PRIMARY)) {
            scan(SAMPLES);
            load(0);
        }
    }
    /* alt çubuk */
    Rect bar = { 0, H - BAR, W, BAR };
    fill_rect(s, bar, T.surface);
    fill_rect(s, (Rect){ 0, bar.y, W, 1 }, T.border);
    if (ui_button(s, u, (Rect){ 12, bar.y + 10, 40, 32 }, "‹", BTN_NORMAL) && nfiles)
        load((cur - 1 + nfiles) % nfiles);
    if (ui_button(s, u, (Rect){ 58, bar.y + 10, 40, 32 }, "›", BTN_NORMAL) && nfiles)
        load((cur + 1) % nfiles);
    if (ui_button(s, u, (Rect){ W - 150, bar.y + 10, 40, 32 }, "−", BTN_NORMAL) && have_img)
        zoom = (zoom > 0 ? zoom : 1) / 1.25f;
    if (ui_button(s, u, (Rect){ W - 106, bar.y + 10, 40, 32 }, "+", BTN_NORMAL) && have_img)
        zoom = (zoom > 0 ? zoom : 1) * 1.25f;
    if (ui_button(s, u, (Rect){ W - 62, bar.y + 10, 50, 32 }, "Sığdır", BTN_GHOST))
        zoom = 0;
    if (have_img && cur >= 0) {
        char info[400], sz[32];
        fmt_size(sz, sizeof sz, fsize);
        snprintf(info, sizeof info, "%s   •   %d × %d   •   %s   •   %d / %d", files[cur], img.w, img.h, sz, cur + 1, nfiles);
        draw_text_fit(s, F_UI, 13, 112, bar.y + 17, W - 112 - 170, T.subtext, info);
    }
}

static void key(KeyEv *e)
{
    if (!e->down || !nfiles)
        return;
    if (e->code == KEY_RIGHT || e->code == KEY_SPACE || e->code == KEY_PAGEDOWN)
        load((cur + 1) % nfiles);
    else if (e->code == KEY_LEFT || e->code == KEY_BACKSPACE || e->code == KEY_PAGEUP)
        load((cur - 1 + nfiles) % nfiles);
    else if (e->ch == '+' || e->ch == '=')
        zoom = (zoom > 0 ? zoom : 1) * 1.25f;
    else if (e->ch == '-')
        zoom = (zoom > 0 ? zoom : 1) / 1.25f;
    else if (e->ch == '0')
        zoom = 0;
}

static void mouse(MouseEv *e)
{
    if (e->kind == M_WHEEL && have_img) {
        zoom = (zoom > 0 ? zoom : 1) * (e->wheel > 0 ? 1.15f : 1 / 1.15f);
        axsapp_redraw();
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && argv[1][0]) {
        char p[PATH_MAX];
        snprintf(p, sizeof p, "%s", argv[1]);
        char *sl = strrchr(p, '/');
        const char *name = argv[1];
        if (sl) {
            *sl = 0;
            name = sl + 1;
            scan(p[0] ? p : "/");
        } else {
            scan(".");
        }
        for (int i = 0; i < nfiles; i++)
            if (!strcmp(files[i], name))
                cur = i;
        load(cur >= 0 ? cur : 0);
    } else {
        scan(SAMPLES);
        load(0);
    }
    AxsAppFuncs f = { .draw = draw, .key = key, .mouse = mouse };
    return axsapp_run("resim-gorucu", "Resim Görüntüleyici", 820, 580, &f);
}
