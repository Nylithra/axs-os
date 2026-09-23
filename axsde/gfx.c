/* AxsDE - yazılımla çizim: dikdörtgenler, yuvarlak köşeler, gölge, bulanıklık, çizgiler */
#include "axsde.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Dikdörtgenler                                                       */
/* ------------------------------------------------------------------ */

Rect rect_isect(Rect a, Rect b)
{
    int x0 = maxi(a.x, b.x), y0 = maxi(a.y, b.y);
    int x1 = mini(a.x + a.w, b.x + b.w), y1 = mini(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return (Rect){ 0, 0, 0, 0 };
    return (Rect){ x0, y0, x1 - x0, y1 - y0 };
}

Rect rect_union(Rect a, Rect b)
{
    if (rect_empty(a))
        return b;
    if (rect_empty(b))
        return a;
    int x0 = mini(a.x, b.x), y0 = mini(a.y, b.y);
    int x1 = maxi(a.x + a.w, b.x + b.w), y1 = maxi(a.y + a.h, b.y + b.h);
    return (Rect){ x0, y0, x1 - x0, y1 - y0 };
}

int rect_empty(Rect r) { return r.w <= 0 || r.h <= 0; }

int rect_has(Rect r, int x, int y)
{
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

Rect rect_inset(Rect r, int d)
{
    return (Rect){ r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d };
}

/* ------------------------------------------------------------------ */
/* Yüzeyler                                                            */
/* ------------------------------------------------------------------ */

Surf surf_new(int w, int h)
{
    Surf s = { 0 };
    w = maxi(w, 1);
    h = maxi(h, 1);
    s.px = calloc((size_t)w * h, 4);
    s.w = w;
    s.h = h;
    s.stride = w;
    s.clip = (Rect){ 0, 0, w, h };
    return s;
}

void surf_free(Surf *s)
{
    free(s->px);
    s->px = NULL;
    s->w = s->h = 0;
}

void surf_resize(Surf *s, int w, int h)
{
    if (s->px && s->w == w && s->h == h)
        return;
    surf_free(s);
    *s = surf_new(w, h);
}

void surf_clip(Surf *s, Rect r) { s->clip = rect_isect(r, (Rect){ 0, 0, s->w, s->h }); }
void surf_noclip(Surf *s) { s->clip = (Rect){ 0, 0, s->w, s->h }; }

/* ------------------------------------------------------------------ */
/* Renk                                                                */
/* ------------------------------------------------------------------ */

static inline uint32_t blend_px(uint32_t d, uint32_t s, uint32_t a)
{
    if (a >= 255)
        return s | 0xFF000000u;
    if (a == 0)
        return d;
    uint32_t ia = 255 - a;
    uint32_t rb = (((s & 0xFF00FF) * a + (d & 0xFF00FF) * ia + 0x800080) >> 8) & 0xFF00FF;
    uint32_t g = (((s & 0x00FF00) * a + (d & 0x00FF00) * ia + 0x008000) >> 8) & 0x00FF00;
    return 0xFF000000u | rb | g;
}

uint32_t mix(uint32_t a, uint32_t b, int t)
{
    t = clampi(t, 0, 255);
    uint32_t aa = (a >> 24) + ((((b >> 24) - (int)(a >> 24)) * t) >> 8);
    uint32_t c = blend_px(a, b, t) & 0xFFFFFF;
    return (aa << 24) | c;
}

uint32_t lighten(uint32_t c, int amt)
{
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (amt > 0) {
        r += (255 - r) * amt / 255;
        g += (255 - g) * amt / 255;
        b += (255 - b) * amt / 255;
    } else {
        r = r * (255 + amt) / 255;
        g = g * (255 + amt) / 255;
        b = b * (255 + amt) / 255;
    }
    return (c & 0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static inline void put(Surf *s, int x, int y, uint32_t c, int a)
{
    uint32_t *p = &s->px[y * s->stride + x];
    *p = blend_px(*p, c, (uint32_t)a);
}

/* ------------------------------------------------------------------ */
/* Dolgular                                                            */
/* ------------------------------------------------------------------ */

void fill_rect(Surf *s, Rect r, uint32_t c)
{
    Rect k = rect_isect(r, s->clip);
    if (rect_empty(k))
        return;
    int a = CA(c);
    for (int y = k.y; y < k.y + k.h; y++) {
        uint32_t *p = &s->px[y * s->stride + k.x];
        if (a >= 255) {
            for (int x = 0; x < k.w; x++)
                p[x] = c;
        } else {
            for (int x = 0; x < k.w; x++)
                p[x] = blend_px(p[x], c, a);
        }
    }
}

/* Köşe kaplama oranı (0..256): (px,py) pikselinin, merkezi (cx,cy) olan
 * yarıçapı rad çeyrek dairenin içinde kalan kısmı. */
static inline int corner_cov(int px, int py, float cx, float cy, float rad)
{
    float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
    float d = sqrtf(dx * dx + dy * dy);
    float v = rad - d + 0.5f;
    if (v <= 0)
        return 0;
    if (v >= 1)
        return 256;
    return (int)(v * 256);
}

static void rrect_impl(Surf *s, Rect r, int rad, uint32_t top, uint32_t bot, int corners)
{
    Rect k = rect_isect(r, s->clip);
    if (rect_empty(k))
        return;
    rad = mini(rad, mini(r.w, r.h) / 2);
    int grad = top != bot;
    for (int y = k.y; y < k.y + k.h; y++) {
        uint32_t c = grad ? mix(top, bot, r.h > 1 ? (y - r.y) * 255 / (r.h - 1) : 0) : top;
        int a = CA(c);
        uint32_t *row = &s->px[y * s->stride];
        int in_top = y < r.y + rad, in_bot = y >= r.y + r.h - rad;
        int cl = 0, cr = 0; /* bu satırda sol/sağ köşe var mı */
        float cy = 0;
        if (in_top) {
            cl = corners & 1;
            cr = corners & 2;
            cy = r.y + rad;
        } else if (in_bot) {
            cl = corners & 4;
            cr = corners & 8;
            cy = r.y + r.h - rad;
        }
        int x0 = k.x, x1 = k.x + k.w;
        if (cl) {
            int e = mini(r.x + rad, x1);
            for (int x = x0; x < e; x++) {
                int cov = corner_cov(x, y, r.x + rad, cy, rad);
                if (cov)
                    row[x] = blend_px(row[x], c, (uint32_t)(a * cov >> 8));
            }
            x0 = maxi(x0, r.x + rad);
        }
        if (cr) {
            int b = maxi(r.x + r.w - rad, x0);
            for (int x = b; x < x1; x++) {
                int cov = corner_cov(x, y, r.x + r.w - rad, cy, rad);
                if (cov)
                    row[x] = blend_px(row[x], c, (uint32_t)(a * cov >> 8));
            }
            x1 = mini(x1, r.x + r.w - rad);
        }
        if (a >= 255)
            for (int x = x0; x < x1; x++)
                row[x] = c;
        else
            for (int x = x0; x < x1; x++)
                row[x] = blend_px(row[x], c, a);
    }
}

void fill_rrect(Surf *s, Rect r, int rad, uint32_t c) { rrect_impl(s, r, rad, c, c, 15); }
void fill_rrect_vgrad(Surf *s, Rect r, int rad, uint32_t t, uint32_t b) { rrect_impl(s, r, rad, t, b, 15); }
void fill_rrect_corners(Surf *s, Rect r, int rad, uint32_t c, int corners) { rrect_impl(s, r, rad, c, c, corners); }

/* İşaretli uzaklık: yuvarlak dikdörtgen */
static inline float sd_rrect(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float in = fmaxf(qx, qy);
    return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

void stroke_rrect(Surf *s, Rect r, int rad, int width, uint32_t c)
{
    Rect k = rect_isect(r, s->clip);
    if (rect_empty(k))
        return;
    rad = mini(rad, mini(r.w, r.h) / 2);
    float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f, hw = r.w / 2.0f, hh = r.h / 2.0f;
    int a = CA(c);
    int band = maxi(rad, width) + 1;
    for (int y = k.y; y < k.y + k.h; y++) {
        int full = y < r.y + band || y >= r.y + r.h - band;
        for (int x = k.x; x < k.x + k.w; x++) {
            if (!full && x >= r.x + width + 1 && x < r.x + r.w - width - 1) {
                x = r.x + r.w - width - 2;
                continue;
            }
            float d = sd_rrect(x + 0.5f, y + 0.5f, cx, cy, hw, hh, (float)rad);
            float outer = 0.5f - d, inner = 0.5f - (d + width);
            outer = outer < 0 ? 0 : outer > 1 ? 1 : outer;
            inner = inner < 0 ? 0 : inner > 1 ? 1 : inner;
            float cov = outer - inner;
            if (cov > 0.004f)
                put(s, x, y, c, (int)(a * cov));
        }
    }
}

/* SDF tabanlı genel boyama: bbox içindeki her piksel için uzaklık -> kaplama */
typedef float (*SdfFn)(float x, float y, const float *p);

static void sdf_fill(Surf *s, Rect bbox, SdfFn fn, const float *p, uint32_t c1, uint32_t c2)
{
    Rect k = rect_isect(bbox, s->clip);
    if (rect_empty(k))
        return;
    int grad = c1 != c2;
    for (int y = k.y; y < k.y + k.h; y++) {
        uint32_t c = grad ? mix(c1, c2, bbox.h > 1 ? (y - bbox.y) * 255 / (bbox.h - 1) : 0) : c1;
        int a = CA(c);
        for (int x = k.x; x < k.x + k.w; x++) {
            float d = fn(x + 0.5f, y + 0.5f, p);
            float cov = 0.5f - d;
            if (cov <= 0)
                continue;
            if (cov > 1)
                cov = 1;
            put(s, x, y, c, (int)(a * cov));
        }
    }
}

static float sdf_circle(float x, float y, const float *p)
{
    float dx = x - p[0], dy = y - p[1];
    return sqrtf(dx * dx + dy * dy) - p[2];
}

static float sdf_ring(float x, float y, const float *p)
{
    float dx = x - p[0], dy = y - p[1];
    return fabsf(sqrtf(dx * dx + dy * dy) - p[2]) - p[3] / 2;
}

static float sd_capsule(float x, float y, float ax, float ay, float bx, float by, float r)
{
    float pax = x - ax, pay = y - ay, bax = bx - ax, bay = by - ay;
    float den = bax * bax + bay * bay;
    float h = den > 0 ? (pax * bax + pay * bay) / den : 0;
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    float dx = pax - bax * h, dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy) - r;
}

static float sdf_capsule(float x, float y, const float *p)
{
    return sd_capsule(x, y, p[0], p[1], p[2], p[3], p[4]);
}

void fill_circle(Surf *s, float cx, float cy, float rad, uint32_t c)
{
    float p[3] = { cx, cy, rad };
    Rect bb = { (int)floorf(cx - rad - 1), (int)floorf(cy - rad - 1), (int)(2 * rad + 3), (int)(2 * rad + 3) };
    sdf_fill(s, bb, sdf_circle, p, c, c);
}

void stroke_circle(Surf *s, float cx, float cy, float rad, float width, uint32_t c)
{
    float p[4] = { cx, cy, rad, width };
    float e = rad + width;
    Rect bb = { (int)floorf(cx - e - 1), (int)floorf(cy - e - 1), (int)(2 * e + 3), (int)(2 * e + 3) };
    sdf_fill(s, bb, sdf_ring, p, c, c);
}

void draw_line(Surf *s, float x0, float y0, float x1, float y1, float width, uint32_t c)
{
    float r = width / 2;
    float p[5] = { x0, y0, x1, y1, r };
    int bx = (int)floorf(fminf(x0, x1) - r - 1), by = (int)floorf(fminf(y0, y1) - r - 1);
    Rect bb = { bx, by, (int)(fabsf(x1 - x0) + 2 * r + 3), (int)(fabsf(y1 - y0) + 2 * r + 3) };
    sdf_fill(s, bb, sdf_capsule, p, c, c);
}

/* ------------------------------------------------------------------ */
/* Gölge                                                               */
/* ------------------------------------------------------------------ */

void draw_shadow(Surf *s, Rect r, int rad, int blur, int dy, int alpha)
{
    Rect sr = { r.x, r.y + dy, r.w, r.h };
    Rect bb = { sr.x - blur, sr.y - blur, sr.w + 2 * blur, sr.h + 2 * blur };
    Rect k = rect_isect(bb, s->clip);
    if (rect_empty(k))
        return;
    float cx = sr.x + sr.w / 2.0f, cy = sr.y + sr.h / 2.0f, hw = sr.w / 2.0f, hh = sr.h / 2.0f;
    float inv = 1.0f / blur;
    /* Pencerenin kendisinin kesin örteceği alan (köşeler hariç) atlanır */
    int sx0 = r.x + rad, sx1 = r.x + r.w - rad;
    for (int y = k.y; y < k.y + k.h; y++) {
        int covered_row = y >= r.y && y < r.y + r.h;
        for (int x = k.x; x < k.x + k.w; x++) {
            if (covered_row && x >= sx0 && x < sx1) {
                x = sx1 - 1;
                continue;
            }
            if (covered_row && y >= r.y + rad && y < r.y + r.h - rad && x >= r.x && x < r.x + r.w) {
                continue;
            }
            float d = sd_rrect(x + 0.5f, y + 0.5f, cx, cy, hw, hh, (float)rad);
            float t = d <= 0 ? 0 : d * inv;
            if (t >= 1)
                continue;
            float f = (1 - t);
            f = f * f * f;
            put(s, x, y, 0, (int)(alpha * f));
        }
    }
}

/* ------------------------------------------------------------------ */
/* Kopyalama                                                           */
/* ------------------------------------------------------------------ */

void blit(Surf *dst, int dx, int dy, const Surf *src, Rect sr)
{
    Rect dr = { dx, dy, sr.w, sr.h };
    Rect k = rect_isect(dr, dst->clip);
    if (rect_empty(k))
        return;
    int ox = sr.x + (k.x - dx), oy = sr.y + (k.y - dy);
    if (ox < 0 || oy < 0 || ox + k.w > src->w || oy + k.h > src->h)
        return;
    for (int y = 0; y < k.h; y++)
        memcpy(&dst->px[(k.y + y) * dst->stride + k.x], &src->px[(oy + y) * src->stride + ox], (size_t)k.w * 4);
}

void blit_rrect(Surf *dst, int dx, int dy, const Surf *src, Rect sr, int rad, int corners)
{
    Rect dr = { dx, dy, sr.w, sr.h };
    Rect k = rect_isect(dr, dst->clip);
    if (rect_empty(k))
        return;
    for (int y = k.y; y < k.y + k.h; y++) {
        const uint32_t *sp = &src->px[(sr.y + y - dy) * src->stride + sr.x - dx];
        uint32_t *dp = &dst->px[y * dst->stride];
        int in_top = y < dy + rad, in_bot = y >= dy + sr.h - rad;
        int cl = (in_top && (corners & 1)) || (in_bot && (corners & 4));
        int cr = (in_top && (corners & 2)) || (in_bot && (corners & 8));
        float cy = in_top ? dy + rad : dy + sr.h - rad;
        int x0 = k.x, x1 = k.x + k.w;
        if (cl) {
            int e = mini(dx + rad, x1);
            for (int x = x0; x < e; x++) {
                int cov = corner_cov(x, y, dx + rad, cy, rad);
                if (cov)
                    dp[x] = blend_px(dp[x], sp[x], (uint32_t)(255 * cov >> 8));
            }
            x0 = maxi(x0, dx + rad);
        }
        if (cr) {
            int b = maxi(dx + sr.w - rad, x0);
            for (int x = b; x < x1; x++) {
                int cov = corner_cov(x, y, dx + sr.w - rad, cy, rad);
                if (cov)
                    dp[x] = blend_px(dp[x], sp[x], (uint32_t)(255 * cov >> 8));
            }
            x1 = mini(x1, dx + sr.w - rad);
        }
        if (x1 > x0)
            memcpy(&dp[x0], &sp[x0], (size_t)(x1 - x0) * 4);
    }
}

/* ------------------------------------------------------------------ */
/* Bulanıklık (buzlu cam paneller)                                     */
/* ------------------------------------------------------------------ */

static void box_h(uint32_t *src, uint32_t *dst, int w, int h, int stride, int r)
{
    int div = 2 * r + 1;
    for (int y = 0; y < h; y++) {
        uint32_t *s = src + y * stride, *d = dst + y * w;
        int sr = 0, sg = 0, sb = 0;
        for (int i = -r; i <= r; i++) {
            uint32_t p = s[clampi(i, 0, w - 1)];
            sr += (p >> 16) & 255;
            sg += (p >> 8) & 255;
            sb += p & 255;
        }
        for (int x = 0; x < w; x++) {
            d[x] = 0xFF000000u | ((uint32_t)(sr / div) << 16) | ((uint32_t)(sg / div) << 8) | (uint32_t)(sb / div);
            uint32_t pa = s[mini(x + r + 1, w - 1)], pr = s[maxi(x - r, 0)];
            sr += (int)((pa >> 16) & 255) - (int)((pr >> 16) & 255);
            sg += (int)((pa >> 8) & 255) - (int)((pr >> 8) & 255);
            sb += (int)(pa & 255) - (int)(pr & 255);
        }
    }
}

static void box_v(uint32_t *src, uint32_t *dst, int w, int h, int dstride, int r)
{
    int div = 2 * r + 1;
    for (int x = 0; x < w; x++) {
        int sr = 0, sg = 0, sb = 0;
        for (int i = -r; i <= r; i++) {
            uint32_t p = src[clampi(i, 0, h - 1) * w + x];
            sr += (p >> 16) & 255;
            sg += (p >> 8) & 255;
            sb += p & 255;
        }
        for (int y = 0; y < h; y++) {
            dst[y * dstride + x] = 0xFF000000u | ((uint32_t)(sr / div) << 16) | ((uint32_t)(sg / div) << 8) | (uint32_t)(sb / div);
            uint32_t pa = src[mini(y + r + 1, h - 1) * w + x], pr = src[maxi(y - r, 0) * w + x];
            sr += (int)((pa >> 16) & 255) - (int)((pr >> 16) & 255);
            sg += (int)((pa >> 8) & 255) - (int)((pr >> 8) & 255);
            sb += (int)(pa & 255) - (int)(pr & 255);
        }
    }
}

void blur_region(Surf *s, Rect r, int radius)
{
    r = rect_isect(r, (Rect){ 0, 0, s->w, s->h });
    if (rect_empty(r) || radius < 1)
        return;
    static uint32_t *tmp, *tmp2;
    static size_t cap;
    size_t n = (size_t)r.w * r.h;
    if (n > cap) {
        free(tmp);
        free(tmp2);
        tmp = malloc(n * 4);
        tmp2 = malloc(n * 4);
        cap = n;
    }
    uint32_t *base = &s->px[r.y * s->stride + r.x];
    /* iki geçiş ≈ üçgen çekirdek, gözle Gauss'a yakın */
    box_h(base, tmp, r.w, r.h, s->stride, radius);
    box_v(tmp, tmp2, r.w, r.h, r.w, radius);
    box_h(tmp2, tmp, r.w, r.h, r.w, radius);
    box_v(tmp, base, r.w, r.h, s->stride, radius);
}

/* ------------------------------------------------------------------ */
/* İmleç ve logo                                                       */
/* ------------------------------------------------------------------ */

static const char *CURSOR[] = {
    "X",
    "XX",
    "X.X",
    "X..X",
    "X...X",
    "X....X",
    "X.....X",
    "X......X",
    "X.......X",
    "X........X",
    "X.........X",
    "X..........X",
    "X......XXXXX",
    "X...X..X",
    "X..XX..X",
    "X.X  X..X",
    "XX   X..X",
    "X     X..X",
    "      X..X",
    "       XX",
    NULL
};

void draw_arrow_cursor(Surf *s, int x, int y)
{
    /* yumuşak gölge */
    for (int j = 0; CURSOR[j]; j++)
        for (int i = 0; CURSOR[j][i]; i++)
            if (CURSOR[j][i] != ' ') {
                int px = x + i + 1, py = y + j + 2;
                if (rect_has(s->clip, px, py))
                    put(s, px, py, 0, 60);
            }
    for (int j = 0; CURSOR[j]; j++)
        for (int i = 0; CURSOR[j][i]; i++) {
            char c = CURSOR[j][i];
            if (c == ' ')
                continue;
            int px = x + i, py = y + j;
            if (!rect_has(s->clip, px, py))
                continue;
            s->px[py * s->stride + px] = c == 'X' ? RGB(20, 20, 28) : RGB(255, 255, 255);
        }
}

static float sdf_logo(float x, float y, const float *p)
{
    /* p: cx, cy, size */
    float cx = p[0], cy = p[1], sz = p[2];
    float r = sz * 0.085f;
    float tx = cx, ty = cy - sz * 0.40f;
    float lx = cx - sz * 0.34f, ly = cy + sz * 0.38f;
    float rx = cx + sz * 0.34f, ry = ly;
    float d = sd_capsule(x, y, tx, ty, lx, ly, r);
    d = fminf(d, sd_capsule(x, y, tx, ty, rx, ry, r));
    d = fminf(d, sd_capsule(x, y, cx - sz * 0.15f, cy + sz * 0.10f, cx + sz * 0.15f, cy + sz * 0.10f, r * 0.8f));
    return d;
}

void draw_logo(Surf *s, float cx, float cy, float size, uint32_t c1, uint32_t c2)
{
    float p[3] = { cx, cy, size };
    Rect bb = { (int)(cx - size / 2 - 2), (int)(cy - size / 2 - 2), (int)size + 4, (int)size + 4 };
    sdf_fill(s, bb, sdf_logo, p, c1, c2);
}
