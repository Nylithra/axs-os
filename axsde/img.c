/* AxsDE - PNG/JPG yükleme (stb_image), kaliteli küçültme ve alfa karıştırmalı kopyalama */
#include "axsde.h"

#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int img_load(const char *path, Surf *out)
{
    int w, h, n;
    unsigned char *d = stbi_load(path, &w, &h, &n, 4);
    if (!d)
        return -1;
    *out = surf_new(w, h);
    for (int i = 0; i < w * h; i++) {
        unsigned char *p = d + i * 4;
        out->px[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    }
    stbi_image_free(d);
    return 0;
}

/* Alan ortalamalı küçültme (alfa ile ağırlıklı: kenarlarda renk saçağı olmaz) */
static Surf scale_down(const Surf *src, int size)
{
    Surf d = surf_new(size, size);
    for (int y = 0; y < size; y++) {
        int y0 = y * src->h / size, y1 = maxi(y0 + 1, (y + 1) * src->h / size);
        for (int x = 0; x < size; x++) {
            int x0 = x * src->w / size, x1 = maxi(x0 + 1, (x + 1) * src->w / size);
            uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
            for (int j = y0; j < y1; j++)
                for (int i = x0; i < x1; i++) {
                    uint32_t p = src->px[j * src->stride + i];
                    uint32_t a = p >> 24;
                    sa += a;
                    sr += ((p >> 16) & 255) * a;
                    sg += ((p >> 8) & 255) * a;
                    sb += (p & 255) * a;
                    n++;
                }
            uint32_t v = 0;
            if (sa)
                v = ((sa / n) << 24) | ((sr / sa) << 16) | ((sg / sa) << 8) | (sb / sa);
            d.px[y * size + x] = v;
        }
    }
    return d;
}

typedef struct { char path[256]; int size; Surf s; } IconCache;
static IconCache icache[64];
static int nicache;

const Surf *icon_png(const char *path, int size)
{
    if (!path || !*path)
        return NULL;
    for (int i = 0; i < nicache; i++)
        if (icache[i].size == size && !strcmp(icache[i].path, path))
            return icache[i].s.px ? &icache[i].s : NULL;
    Surf src;
    IconCache *c = &icache[nicache < 64 ? nicache++ : 63];
    if (c->s.px)
        surf_free(&c->s);
    snprintf(c->path, sizeof c->path, "%s", path);
    c->size = size;
    c->s = (Surf){ 0 };
    if (img_load(path, &src) != 0)
        return NULL; /* başarısızlık da önbelleğe alındı: tekrar denenmez */
    c->s = scale_down(&src, size);
    surf_free(&src);
    return &c->s;
}

void blit_alpha(Surf *dst, int dx, int dy, const Surf *src)
{
    Rect k = rect_isect((Rect){ dx, dy, src->w, src->h }, dst->clip);
    for (int y = k.y; y < k.y + k.h; y++) {
        const uint32_t *sp = &src->px[(y - dy) * src->stride + (k.x - dx)];
        uint32_t *dp = &dst->px[y * dst->stride + k.x];
        for (int x = 0; x < k.w; x++) {
            uint32_t p = sp[x], a = p >> 24;
            if (!a)
                continue;
            if (a == 255) {
                dp[x] = p;
                continue;
            }
            uint32_t d = dp[x], ia = 255 - a;
            uint32_t rb = (((p & 0xFF00FF) * a + (d & 0xFF00FF) * ia + 0x800080) >> 8) & 0xFF00FF;
            uint32_t g = (((p & 0x00FF00) * a + (d & 0x00FF00) * ia + 0x008000) >> 8) & 0x00FF00;
            dp[x] = 0xFF000000u | rb | g;
        }
    }
}
