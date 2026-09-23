/* AxsDE - TrueType metin çizimi (stb_truetype) ve glif önbelleği */
#include "axsde.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define FONT_DIR "/usr/share/fonts/axsde/"

typedef struct {
    stbtt_fontinfo info;
    unsigned char *data;
    int ascent, descent, gap;
    int synth_bold;
    int ok;
} Font;

static Font fonts[F_COUNT];

typedef struct {
    uint32_t key_cp;
    uint16_t key_size;
    uint8_t key_font, used;
    int16_t w, h, xoff, yoff, adv;
    uint8_t *bm;
} Glyph;

#define CACHE_SIZE 4096
static Glyph cache[CACHE_SIZE];
static int cache_count;

static unsigned char *load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *d = malloc(n);
    if (d && fread(d, 1, n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    return d;
}

static int font_load(Font *f, const char *file, int bold)
{
    char path[512];
    const char *dir = getenv("AXSDE_FONTS"); /* ölçüm/test için başka dizin */
    snprintf(path, sizeof path, "%s/%s", dir ? dir : FONT_DIR, file);
    f->data = load(path);
    if (!f->data || !stbtt_InitFont(&f->info, f->data, stbtt_GetFontOffsetForIndex(f->data, 0))) {
        fprintf(stderr, "axsde: yazı tipi yüklenemedi: %s\n", path);
        return -1;
    }
    stbtt_GetFontVMetrics(&f->info, &f->ascent, &f->descent, &f->gap);
    f->synth_bold = bold;
    f->ok = 1;
    return 0;
}

int font_init(void)
{
    int rc = 0;
    rc |= font_load(&fonts[F_UI], "Inter.ttf", 0);
    rc |= font_load(&fonts[F_UI_BOLD], "Inter.ttf", 1);
    rc |= font_load(&fonts[F_MONO], "JetBrainsMono-Regular.ttf", 0);
    rc |= font_load(&fonts[F_MONO_BOLD], "JetBrainsMono-Bold.ttf", 0);
    return rc;
}

static float scale_of(Font *f, int size)
{
    return stbtt_ScaleForMappingEmToPixels(&f->info, (float)size);
}

int font_ascent(FontId id, int size)
{
    Font *f = &fonts[id];
    return (int)(f->ascent * scale_of(f, size) + 0.5f);
}

int font_height(FontId id, int size)
{
    Font *f = &fonts[id];
    return (int)((f->ascent - f->descent + f->gap) * scale_of(f, size) + 0.5f);
}

static Glyph *glyph_get(FontId id, int size, uint32_t cp)
{
    uint32_t h = (cp * 2654435761u) ^ ((uint32_t)size * 40503u) ^ ((uint32_t)id * 97u);
    for (int i = 0; i < CACHE_SIZE; i++) {
        Glyph *g = &cache[(h + i) & (CACHE_SIZE - 1)];
        if (!g->used)
            break;
        if (g->key_cp == cp && g->key_size == size && g->key_font == id)
            return g;
    }
    if (cache_count > CACHE_SIZE * 3 / 4) {
        for (int i = 0; i < CACHE_SIZE; i++) {
            free(cache[i].bm);
            cache[i] = (Glyph){ 0 };
        }
        cache_count = 0;
    }
    Font *f = &fonts[id];
    FontId use = id;
    if (!stbtt_FindGlyphIndex(&f->info, (int)cp)) {
        /* yedek: diğer yazı tipi ailesi */
        FontId alt = (id == F_MONO || id == F_MONO_BOLD) ? F_UI : F_MONO;
        if (fonts[alt].ok && stbtt_FindGlyphIndex(&fonts[alt].info, (int)cp))
            use = alt;
    }
    Font *uf = &fonts[use];
    float sc = scale_of(uf, size);
    Glyph *g = NULL;
    for (int i = 0; i < CACHE_SIZE; i++) {
        Glyph *c = &cache[(h + i) & (CACHE_SIZE - 1)];
        if (!c->used) {
            g = c;
            break;
        }
    }
    if (!g)
        return NULL;
    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(&uf->info, (int)cp, &adv, &lsb);
    stbtt_GetCodepointBitmapBox(&uf->info, (int)cp, sc, sc, &x0, &y0, &x1, &y1);
    g->used = 1;
    g->key_cp = cp;
    g->key_size = (uint16_t)size;
    g->key_font = (uint8_t)id;
    g->w = (int16_t)(x1 - x0);
    g->h = (int16_t)(y1 - y0);
    g->xoff = (int16_t)x0;
    g->yoff = (int16_t)y0;
    g->adv = (int16_t)(adv * sc + 0.5f);
    g->bm = NULL;
    int bold = f->synth_bold;
    if (g->w > 0 && g->h > 0) {
        int bw = g->w + (bold ? 1 : 0);
        g->bm = calloc((size_t)bw * g->h, 1);
        stbtt_MakeCodepointBitmap(&uf->info, g->bm, g->w, g->h, bw, sc, sc, (int)cp);
        if (bold) {
            /* sentetik kalın: yatayda 1 piksel genişlet */
            for (int y = 0; y < g->h; y++) {
                uint8_t *row = g->bm + y * bw;
                for (int x = bw - 1; x > 0; x--) {
                    int v = row[x] + row[x - 1] * 3 / 4;
                    row[x] = (uint8_t)(v > 255 ? 255 : v);
                }
            }
            g->w = (int16_t)bw;
            g->adv += 1;
        }
    }
    cache_count++;
    return g;
}

/* ------------------------------------------------------------------ */
/* UTF-8                                                               */
/* ------------------------------------------------------------------ */

uint32_t utf8_next(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    uint32_t c = *p;
    int n = 0;
    if (c < 0x80) {
        n = 1;
    } else if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        c = ((c & 0x1F) << 6) | (p[1] & 0x3F);
        n = 2;
    } else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        n = 3;
    } else if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
        c = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        n = 4;
    } else {
        c = 0xFFFD;
        n = 1;
    }
    *pp += n;
    return c;
}

int utf8_prev(const char *s, int pos)
{
    if (pos <= 0)
        return 0;
    pos--;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0) == 0x80)
        pos--;
    return pos;
}

int utf8_len(uint32_t cp)
{
    return cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
}

int utf8_put(char *o, uint32_t cp)
{
    if (cp < 0x80) {
        o[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | (cp >> 6));
        o[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12));
        o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18));
    o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* ------------------------------------------------------------------ */
/* Çizim                                                               */
/* ------------------------------------------------------------------ */

static inline uint32_t blend_a(uint32_t d, uint32_t s, uint32_t a)
{
    uint32_t ia = 255 - a;
    uint32_t rb = (((s & 0xFF00FF) * a + (d & 0xFF00FF) * ia + 0x800080) >> 8) & 0xFF00FF;
    uint32_t g = (((s & 0x00FF00) * a + (d & 0x00FF00) * ia + 0x008000) >> 8) & 0x00FF00;
    return 0xFF000000u | rb | g;
}

static void blit_glyph(Surf *s, Glyph *g, int x, int y, uint32_t c)
{
    if (!g->bm)
        return;
    Rect gr = { x + g->xoff, y + g->yoff, g->w, g->h };
    Rect k = rect_isect(gr, s->clip);
    if (rect_empty(k))
        return;
    int ca = CA(c);
    for (int j = k.y; j < k.y + k.h; j++) {
        const uint8_t *src = g->bm + (j - gr.y) * g->w + (k.x - gr.x);
        uint32_t *dst = &s->px[j * s->stride + k.x];
        for (int i = 0; i < k.w; i++) {
            uint32_t a = src[i];
            if (!a)
                continue;
            a = a * ca / 255;
            dst[i] = a >= 255 ? (c | 0xFF000000u) : blend_a(dst[i], c, a);
        }
    }
}

void draw_glyph(Surf *s, FontId f, int size, int x, int y, uint32_t c, uint32_t cp)
{
    Glyph *g = glyph_get(f, size, cp);
    if (g)
        blit_glyph(s, g, x, y + font_ascent(f, size), c);
}

int draw_text_n(Surf *s, FontId f, int size, int x, int y, uint32_t c, const char *str, int nbytes)
{
    int base = y + font_ascent(f, size);
    int x0 = x;
    const char *p = str, *end = str + nbytes;
    while (p < end && *p) {
        uint32_t cp = utf8_next(&p);
        if (cp == '\t') {
            Glyph *sp = glyph_get(f, size, ' ');
            x += sp ? sp->adv * 4 : size * 2;
            continue;
        }
        Glyph *g = glyph_get(f, size, cp);
        if (!g)
            continue;
        if (x > s->clip.x + s->clip.w)
            break;
        blit_glyph(s, g, x, base, c);
        x += g->adv;
    }
    return x - x0;
}

int draw_text(Surf *s, FontId f, int size, int x, int y, uint32_t c, const char *str)
{
    return draw_text_n(s, f, size, x, y, c, str, (int)strlen(str));
}

int text_width_n(FontId f, int size, const char *str, int nbytes)
{
    int w = 0;
    const char *p = str, *end = str + nbytes;
    while (p < end && *p) {
        uint32_t cp = utf8_next(&p);
        if (cp == '\t') {
            Glyph *sp = glyph_get(f, size, ' ');
            w += sp ? sp->adv * 4 : size * 2;
            continue;
        }
        Glyph *g = glyph_get(f, size, cp);
        if (g)
            w += g->adv;
    }
    return w;
}

int text_width(FontId f, int size, const char *str)
{
    return text_width_n(f, size, str, (int)strlen(str));
}

int mono_advance(int size)
{
    Glyph *g = glyph_get(F_MONO, size, 'M');
    return g ? g->adv : size * 6 / 10;
}

void draw_text_fit(Surf *s, FontId f, int size, int x, int y, int maxw, uint32_t c, const char *str)
{
    if (text_width(f, size, str) <= maxw) {
        draw_text(s, f, size, x, y, c, str);
        return;
    }
    int ell = text_width(f, size, "…");
    int n = 0, w = 0;
    const char *p = str;
    while (*p) {
        const char *q = p;
        uint32_t cp = utf8_next(&q);
        Glyph *g = glyph_get(f, size, cp);
        int gw = g ? g->adv : 0;
        if (w + gw + ell > maxw)
            break;
        w += gw;
        n += (int)(q - p);
        p = q;
    }
    draw_text_n(s, f, size, x, y, c, str, n);
    draw_text(s, f, size, x + w, y, c, "…");
}

void draw_text_center(Surf *s, FontId f, int size, Rect r, uint32_t c, const char *str)
{
    int w = text_width(f, size, str);
    int h = font_height(f, size);
    draw_text(s, f, size, r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, c, str);
}

void draw_text_wrap(Surf *s, FontId f, int size, int x, int y, int maxw, int maxlines, int lh, uint32_t c, const char *str)
{
    const char *p = str;
    for (int line = 0; line < maxlines && *p; line++) {
        while (*p == ' ')
            p++;
        /* bu satıra sığan en uzun sözcük dizisi */
        const char *q = p, *brk = NULL;
        int w = 0;
        while (*q) {
            const char *nq = q;
            uint32_t cp = utf8_next(&nq);
            Glyph *g = glyph_get(f, size, cp);
            int gw = g ? g->adv : 0;
            if (w + gw > maxw)
                break;
            w += gw;
            q = nq;
            if (*q == ' ' || !*q)
                brk = q;
        }
        if (!*q) {
            draw_text(s, f, size, x, y + line * lh, c, p);
            return;
        }
        if (line == maxlines - 1 || !brk) {
            char tmp[1024];
            snprintf(tmp, sizeof tmp, "%s", p);
            draw_text_fit(s, f, size, x, y + line * lh, maxw, c, tmp);
            return;
        }
        draw_text_n(s, f, size, x, y + line * lh, c, p, (int)(brk - p));
        p = brk;
    }
}
