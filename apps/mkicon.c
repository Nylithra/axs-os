/* mkicon - derleme sırasında (host'ta) uygulama simgelerini ve örnek resimleri üretir.
 *   mkicon icon <kimlik> <çıktı.png> [boyut]
 *   mkicon samples <dizin>
 * AxsDE'nin çizim işlevlerini kullanır. Saydamlık için simge siyah ve beyaz zemine iki kez
 * çizilir; iki sonuç arasındaki farktan alfa hesaplanır.
 */
#include "axsde.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

/* ui.c config_load'un beklediği küreseller */
int wallpaper_idx, tz_offset_min = 180, kb_layout;

static void squircle(Surf *s, int sz, uint32_t top, uint32_t bot)
{
    Rect r = { 0, 0, sz, sz };
    int rad = sz * 23 / 100;
    fill_rrect_vgrad(s, r, rad, top, bot);
    fill_rrect_corners(s, (Rect){ 0, 0, sz, sz / 2 }, rad, ALPHA(HEX(0xFFFFFF), 22), 3);
    stroke_rrect(s, r, rad, 1, ALPHA(HEX(0xFFFFFF), 36));
}

static void tri(Surf *s, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c)
{
    /* basit tarama: 4x4 alt örnekleme */
    int bx0 = (int)fminf(x0, fminf(x1, x2)), by0 = (int)fminf(y0, fminf(y1, y2));
    int bx1 = (int)ceilf(fmaxf(x0, fmaxf(x1, x2))), by1 = (int)ceilf(fmaxf(y0, fmaxf(y1, y2)));
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    float sg = area > 0 ? 1.f : -1.f;
    for (int y = by0; y <= by1; y++)
        for (int x = bx0; x <= bx1; x++) {
            int in = 0;
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    float px = x + (i + .5f) / 4, py = y + (j + .5f) / 4;
                    float e0 = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) * sg;
                    float e1 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) * sg;
                    float e2 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) * sg;
                    in += e0 >= 0 && e1 >= 0 && e2 >= 0;
                }
            if (in)
                fill_rect(s, (Rect){ x, y, 1, 1 }, ALPHA(c, CA(c) * in / 16));
        }
}

/* Dünya küresi (tarayıcı simgeleri; marka logosu değil) */
static void globe(Surf *s, float cx, float cy, float r, float w, uint32_t c)
{
    stroke_circle(s, cx, cy, r, w, c);
    draw_line(s, cx - r, cy, cx + r, cy, w, c);
    draw_line(s, cx, cy - r, cx, cy + r, w, c);
    for (int k = -1; k <= 1; k += 2) { /* enlemler */
        float y = cy + k * r * .5f, hw = r * .866f;
        draw_line(s, cx - hw, y, cx + hw, y, w * .8f, c);
    }
    float px = cx, py = cy - r; /* boylam elipsi */
    for (int i = 1; i <= 48; i++) {
        float a = i * 6.2831853f / 48;
        float nx = cx + r * .45f * sinf(a), ny = cy - r * cosf(a);
        draw_line(s, px, py, nx, ny, w * .8f, c);
        px = nx, py = ny;
    }
}

static int draw_app_icon(Surf *s, const char *id, int sz)
{
    float f = (float)sz;
    uint32_t W = HEX(0xFFFFFF);
#define P(v) ((int)((v) * f))
    if (!strcmp(id, "hesap-makinesi")) {
        squircle(s, sz, HEX(0xFB923C), HEX(0xC2410C));
        fill_rrect(s, (Rect){ P(.24), P(.16), P(.52), P(.68) }, P(.07), HEX(0xFFF7ED));
        fill_rrect(s, (Rect){ P(.30), P(.22), P(.40), P(.14) }, P(.03), HEX(0x7C2D12));
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                fill_rrect(s, (Rect){ P(.30 + c * .14), P(.42 + r * .13), P(.11), P(.10) }, P(.025),
                           c == 2 && r == 2 ? HEX(0xF97316) : HEX(0xFDBA74));
    } else if (!strcmp(id, "yilan")) {
        squircle(s, sz, HEX(0x4ADE80), HEX(0x15803D));
        float pts[][2] = { { .22f, .72f }, { .42f, .72f }, { .42f, .50f }, { .62f, .50f }, { .62f, .30f }, { .76f, .30f } };
        for (int i = 0; i < 5; i++)
            draw_line(s, f * pts[i][0], f * pts[i][1], f * pts[i + 1][0], f * pts[i + 1][1], f * .13f, HEX(0x14532D));
        for (int i = 0; i < 6; i++)
            fill_circle(s, f * pts[i][0], f * pts[i][1], f * .065f, HEX(0x14532D));
        fill_circle(s, f * .76f, f * .30f, f * .09f, HEX(0x14532D));
        fill_circle(s, f * .79f, f * .27f, f * .025f, W);
        fill_circle(s, f * .28f, f * .32f, f * .075f, HEX(0xEF4444));
        draw_line(s, f * .28f, f * .25f, f * .32f, f * .20f, f * .025f, HEX(0x166534));
    } else if (!strcmp(id, "2048")) {
        squircle(s, sz, HEX(0xFCD34D), HEX(0xD97706));
        fill_rrect(s, (Rect){ P(.16), P(.30), P(.68), P(.40) }, P(.08), HEX(0xEDC22E));
        draw_text_center(s, F_UI_BOLD, P(.25), (Rect){ P(.16), P(.30), P(.68), P(.40) }, W, "2048");
    } else if (!strcmp(id, "mayin-tarlasi")) {
        squircle(s, sz, HEX(0x94A3B8), HEX(0x334155));
        float cx = f * .5f, cy = f * .52f, r = f * .2f;
        for (int i = 0; i < 4; i++) {
            float a = i * 3.14159f / 4;
            draw_line(s, cx - r * 1.45f * cosf(a), cy - r * 1.45f * sinf(a), cx + r * 1.45f * cosf(a),
                      cy + r * 1.45f * sinf(a), f * .05f, HEX(0x0F172A));
        }
        fill_circle(s, cx, cy, r, HEX(0x0F172A));
        fill_circle(s, cx - r * .35f, cy - r * .35f, r * .28f, ALPHA(W, 200));
    } else if (!strcmp(id, "resim-gorucu")) {
        squircle(s, sz, HEX(0x38BDF8), HEX(0x0369A1));
        Rect fr = { P(.18), P(.24), P(.64), P(.52) };
        fill_rrect(s, fr, P(.05), W);
        Rect in = rect_inset(fr, P(.045));
        fill_rrect_vgrad(s, in, P(.03), HEX(0xBAE6FD), HEX(0x7DD3FC));
        surf_clip(s, in);
        fill_circle(s, f * .64f, f * .40f, f * .055f, HEX(0xFACC15));
        tri(s, f * .20f, f * .74f, f * .40f, f * .44f, f * .60f, f * .74f, HEX(0x16A34A));
        tri(s, f * .44f, f * .74f, f * .60f, f * .52f, f * .80f, f * .74f, HEX(0x15803D));
        surf_noclip(s);
    } else if (!strcmp(id, "cizim")) {
        squircle(s, sz, HEX(0xF472B6), HEX(0x7E22CE));
        fill_circle(s, f * .46f, f * .52f, f * .28f, HEX(0xFDF4FF));
        fill_circle(s, f * .56f, f * .66f, f * .07f, HEX(0xC026D3)); /* başparmak deliği */
        fill_circle(s, f * .56f, f * .66f, f * .05f, HEX(0xA21CAF));
        uint32_t cols[4] = { HEX(0xEF4444), HEX(0xFACC15), HEX(0x22C55E), HEX(0x3B82F6) };
        float pos[4][2] = { { .32f, .44f }, { .42f, .34f }, { .55f, .36f }, { .34f, .60f } };
        for (int i = 0; i < 4; i++)
            fill_circle(s, f * pos[i][0], f * pos[i][1], f * .05f, cols[i]);
        draw_line(s, f * .60f, f * .56f, f * .82f, f * .20f, f * .06f, HEX(0x78350F));
        fill_circle(s, f * .62f, f * .54f, f * .045f, HEX(0x1F2937));
    } else if (!strcmp(id, "saat")) {
        squircle(s, sz, HEX(0x818CF8), HEX(0x3730A3));
        fill_circle(s, f * .5f, f * .5f, f * .31f, W);
        stroke_circle(s, f * .5f, f * .5f, f * .31f, f * .02f, HEX(0xC7D2FE));
        for (int i = 0; i < 12; i++) {
            float a = i * 6.2831853f / 12;
            draw_line(s, f * .5f + f * .24f * sinf(a), f * .5f - f * .24f * cosf(a), f * .5f + f * .27f * sinf(a),
                      f * .5f - f * .27f * cosf(a), f * (i % 3 ? .012f : .025f), HEX(0x312E81));
        }
        draw_line(s, f * .5f, f * .5f, f * .5f, f * .31f, f * .04f, HEX(0x1E1B4B));
        draw_line(s, f * .5f, f * .5f, f * .64f, f * .58f, f * .04f, HEX(0x1E1B4B));
        draw_line(s, f * .5f, f * .5f, f * .36f, f * .70f, f * .015f, HEX(0xEF4444));
        fill_circle(s, f * .5f, f * .5f, f * .03f, HEX(0xEF4444));
    } else if (!strcmp(id, "takvim")) {
        squircle(s, sz, HEX(0xFFFFFF), HEX(0xE5E7EB));
        fill_rrect_corners(s, (Rect){ 0, 0, sz, P(.30) }, sz * 23 / 100, HEX(0xEF4444), 3);
        draw_text_center(s, F_UI_BOLD, P(.13), (Rect){ 0, P(.06), sz, P(.2) }, W, "EKİM");
        draw_text_center(s, F_UI_BOLD, P(.42), (Rect){ 0, P(.34), sz, P(.56) }, HEX(0x1F2937), "29");
    } else if (!strcmp(id, "merhaba")) {
        squircle(s, sz, HEX(0x2DD4BF), HEX(0x0F766E));
        fill_rrect(s, (Rect){ P(.18), P(.24), P(.64), P(.42) }, P(.12), W);
        tri(s, f * .30f, f * .62f, f * .44f, f * .62f, f * .28f, f * .80f, W);
        for (int i = 0; i < 3; i++)
            fill_circle(s, f * (.36f + i * .14f), f * .45f, f * .045f, HEX(0x0F766E));
    } else if (!strcmp(id, "sistem-bilgi")) {
        squircle(s, sz, HEX(0x60A5FA), HEX(0x1E40AF));
        fill_circle(s, f * .5f, f * .5f, f * .30f, W);
        fill_circle(s, f * .5f, f * .34f, f * .045f, HEX(0x1E40AF));
        fill_rrect(s, (Rect){ P(.455), P(.43), P(.09), P(.27) }, P(.03), HEX(0x1E40AF));
    } else if (!strcmp(id, "atasozu")) {
        squircle(s, sz, HEX(0xFBBF24), HEX(0x92400E));
        fill_rrect(s, (Rect){ P(.16), P(.26), P(.33), P(.48) }, P(.04), HEX(0xFFFBEB));
        fill_rrect(s, (Rect){ P(.51), P(.26), P(.33), P(.48) }, P(.04), HEX(0xFEF3C7));
        for (int i = 0; i < 4; i++) {
            fill_rect(s, (Rect){ P(.22), P(.36 + i * .09), P(.21), P(.025) }, HEX(0xD6B98C));
            fill_rect(s, (Rect){ P(.57), P(.36 + i * .09), P(.21), P(.025) }, HEX(0xD6B98C));
        }
        fill_rect(s, (Rect){ P(.49), P(.26), P(.02), P(.48) }, HEX(0x92400E));
    } else if (!strcmp(id, "carpim-tablosu")) {
        squircle(s, sz, HEX(0xF87171), HEX(0xB91C1C));
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                fill_rrect(s, (Rect){ P(.20 + c * .21), P(.20 + r * .21), P(.18), P(.18) }, P(.04),
                           ALPHA(W, r == 1 && c == 1 ? 255 : 90));
        draw_line(s, f * .45f, f * .45f, f * .55f, f * .55f, f * .035f, HEX(0xB91C1C));
        draw_line(s, f * .55f, f * .45f, f * .45f, f * .55f, f * .035f, HEX(0xB91C1C));
    } else if (!strcmp(id, "sayi-tahmin")) {
        squircle(s, sz, HEX(0xA78BFA), HEX(0x6D28D9));
        fill_circle(s, f * .5f, f * .5f, f * .30f, ALPHA(W, 60));
        draw_text_center(s, F_UI_BOLD, P(.46), (Rect){ 0, P(.24), sz, P(.52) }, W, "?");
    } else if (!strcmp(id, "firefox")) {
        squircle(s, sz, HEX(0xFB923C), HEX(0x7C3AED));
        fill_circle(s, f * .5f, f * .52f, f * .31f, ALPHA(W, 40));
        globe(s, f * .5f, f * .52f, f * .29f, f * .035f, W);
        fill_circle(s, f * .72f, f * .30f, f * .09f, HEX(0xFDE68A));
    } else if (!strcmp(id, "google-chrome")) {
        squircle(s, sz, HEX(0x60A5FA), HEX(0x1E3A8A));
        fill_circle(s, f * .5f, f * .52f, f * .31f, ALPHA(W, 40));
        globe(s, f * .5f, f * .52f, f * .29f, f * .035f, W);
        fill_circle(s, f * .5f, f * .52f, f * .08f, HEX(0x93C5FD));
    } else if (!strcmp(id, "tarayici-ortami")) {
        squircle(s, sz, HEX(0x94A3B8), HEX(0x334155));
        for (int i = 2; i >= 0; i--) {
            Rect r = { P(.22 + i * .06), P(.24 + i * .08), P(.46), P(.34) };
            fill_rrect(s, r, P(.05), i ? ALPHA(W, 90 + (2 - i) * 60) : W);
        }
        fill_rect(s, (Rect){ P(.22), P(.30), P(.46), P(.03) }, HEX(0x334155));
    } else if (!strcmp(id, "axs-ornekler")) {
        squircle(s, sz, HEX(0xC084FC), HEX(0x5B21B6));
        draw_logo(s, f * .5f, f * .52f, f * .56f, W, HEX(0xE9D5FF));
    } else {
        return -1;
    }
#undef P
    return 0;
}

static int write_icon(const char *id, const char *out, int sz)
{
    Surf a = surf_new(sz, sz), b = surf_new(sz, sz);
    fill_rect(&a, (Rect){ 0, 0, sz, sz }, 0xFF000000u);
    fill_rect(&b, (Rect){ 0, 0, sz, sz }, 0xFFFFFFFFu);
    if (draw_app_icon(&a, id, sz) < 0 || draw_app_icon(&b, id, sz) < 0) {
        fprintf(stderr, "mkicon: bilinmeyen simge: %s\n", id);
        return 1;
    }
    unsigned char *o = malloc((size_t)sz * sz * 4);
    for (int i = 0; i < sz * sz; i++) {
        uint32_t pa = a.px[i], pb = b.px[i];
        int al = 255 - (int)((pb >> 8 & 255) - (pa >> 8 & 255)); /* yeşil kanaldan */
        al = clampi(al, 0, 255);
        for (int c = 0; c < 3; c++) {
            int va = (int)(pa >> (16 - 8 * c) & 255);
            o[i * 4 + c] = (unsigned char)(al ? clampi(va * 255 / al, 0, 255) : 0);
        }
        o[i * 4 + 3] = (unsigned char)al;
    }
    int ok = stbi_write_png(out, sz, sz, 4, o, sz * 4);
    free(o);
    return ok ? 0 : 1;
}

/* ---- örnek resimler ---- */
static unsigned char *to_rgb(Surf *s)
{
    unsigned char *o = malloc((size_t)s->w * s->h * 3);
    for (int i = 0; i < s->w * s->h; i++) {
        uint32_t p = s->px[i];
        o[i * 3] = p >> 16 & 255, o[i * 3 + 1] = p >> 8 & 255, o[i * 3 + 2] = p & 255;
    }
    return o;
}

static float hill(float x, float seed)
{
    return sinf(x * 3.1f + seed) * .05f + sinf(x * 7.3f + seed * 2) * .025f + sinf(x * 17.f + seed * 3) * .01f;
}

static void landscape(Surf *s)
{
    int w = s->w, h = s->h;
    for (int y = 0; y < h; y++) {
        float t = (float)y / h;
        uint32_t c = t < .55f ? mix(HEX(0x2B1055), HEX(0xFF8A5B), (int)(t / .55f * 255)) : HEX(0xFF8A5B);
        fill_rect(s, (Rect){ 0, y, w, 1 }, c);
    }
    fill_circle(s, w * .68f, h * .50f, h * .11f, HEX(0xFFE29A));
    fill_circle(s, w * .68f, h * .50f, h * .16f, ALPHA(HEX(0xFFE29A), 50));
    uint32_t layers[4] = { HEX(0x8E4A7A), HEX(0x5E2F6B), HEX(0x3A1D52), HEX(0x1C0F33) };
    float base[4] = { .56f, .64f, .74f, .86f };
    for (int l = 0; l < 4; l++)
        for (int x = 0; x < w; x++) {
            int top = (int)(h * (base[l] + hill((float)x / w, l * 1.7f) * (1.6f - l * .25f)));
            fill_rect(s, (Rect){ x, top, 1, h - top }, layers[l]);
        }
}

static void mandel(Surf *s)
{
    int w = s->w, h = s->h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double cr = 0.2825 + (x - w / 2.0) / w * 0.04, ci = 0.0095 + (y - h / 2.0) / w * 0.04;
            double zr = 0, zi = 0;
            int i = 0;
            for (; i < 300 && zr * zr + zi * zi < 16; i++) {
                double t = zr * zr - zi * zi + cr;
                zi = 2 * zr * zi + ci;
                zr = t;
            }
            uint32_t c = 0xFF05030F;
            if (i < 300) {
                double sm = i + 1 - log2(log2(zr * zr + zi * zi) / 2);
                double k = sm / 40.0;
                int r = (int)(127 + 127 * sin(k * 6.28 + 0.0));
                int g = (int)(127 + 127 * sin(k * 6.28 + 2.1));
                int b = (int)(127 + 127 * sin(k * 6.28 + 4.2));
                c = RGB(clampi(r, 0, 255), clampi(g, 0, 255), clampi(b, 0, 255));
            }
            s->px[y * s->stride + x] = c;
        }
}

static void waves(Surf *s)
{
    int w = s->w, h = s->h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float fx = (float)x / w, fy = (float)y / h;
            float v = sinf(fx * 9 + sinf(fy * 5) * 2) + sinf(fy * 7 + fx * 3) + sinf((fx + fy) * 6);
            float t = (v + 3) / 6;
            uint32_t c = t < .5f ? mix(HEX(0x0EA5E9), HEX(0x8B5CF6), (int)(t * 2 * 255))
                                 : mix(HEX(0x8B5CF6), HEX(0xF472B6), (int)((t - .5f) * 2 * 255));
            s->px[y * s->stride + x] = c;
        }
    surf_noclip(s);
    draw_logo(s, w / 2.f, h / 2.f, h * .45f, ALPHA(HEX(0xFFFFFF), 200), ALPHA(HEX(0xFFFFFF), 90));
}

static int samples(const char *dir)
{
    char p[512];
    Surf s = surf_new(960, 600);
    landscape(&s);
    unsigned char *o = to_rgb(&s);
    snprintf(p, sizeof p, "%s/gun-batimi.jpg", dir);
    stbi_write_jpg(p, s.w, s.h, 3, o, 90);
    free(o);
    mandel(&s);
    o = to_rgb(&s);
    snprintf(p, sizeof p, "%s/fraktal.jpg", dir);
    stbi_write_jpg(p, s.w, s.h, 3, o, 90);
    free(o);
    waves(&s);
    o = to_rgb(&s);
    snprintf(p, sizeof p, "%s/dalgalar.jpg", dir);
    stbi_write_jpg(p, s.w, s.h, 3, o, 90);
    free(o);
    /* saydam PNG: yalnızca logo */
    snprintf(p, sizeof p, "%s/saydam-logo.png", dir);
    return write_icon("axs-ornekler", p, 256);
}

int main(int argc, char **argv)
{
    if (font_init() < 0) {
        fprintf(stderr, "mkicon: yazı tipleri yok (AXSDE_FONTS)\n");
        return 1;
    }
    if (argc >= 4 && !strcmp(argv[1], "icon"))
        return write_icon(argv[2], argv[3], argc > 4 ? atoi(argv[4]) : 128);
    if (argc == 3 && !strcmp(argv[1], "samples"))
        return samples(argv[2]);
    fprintf(stderr, "kullanım: mkicon icon <kimlik> <çıktı.png> [boyut] | mkicon samples <dizin>\n");
    return 2;
}
