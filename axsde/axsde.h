/*
 * AxsDE - AxsOS masaüstü ortamı
 *
 * Tek süreçli, framebuffer'a doğrudan çizen bir masaüstü:
 * pencere yöneticisi + birleştirici (compositor) + yerleşik uygulamalar.
 * Tüm çizim yazılımla yapılır; yalnızca değişen bölgeler yeniden çizilir.
 */
#ifndef AXSDE_H
#define AXSDE_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define AXSDE_VERSION "1.0"

/* ------------------------------------------------------------------ */
/* Temel tipler                                                        */
/* ------------------------------------------------------------------ */

typedef struct { int x, y, w, h; } Rect;

/* 32 bit ARGB yüzey. Ekran ve pencere içerikleri bu türdendir. */
typedef struct {
    uint32_t *px;
    int w, h, stride; /* stride: piksel cinsinden satır uzunluğu */
    Rect clip;        /* çizim bu dikdörtgenle sınırlı */
} Surf;

#define RGB(r, g, b)      (0xFF000000u | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define RGBA(r, g, b, a)  (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define HEX(h)            (0xFF000000u | (uint32_t)(h))
#define ALPHA(c, a)       (((c) & 0x00FFFFFFu) | ((uint32_t)(a) << 24))
#define CA(c)             ((int)((c) >> 24))

static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

Rect rect_isect(Rect a, Rect b);
Rect rect_union(Rect a, Rect b);
int  rect_empty(Rect r);
int  rect_has(Rect r, int x, int y);
Rect rect_inset(Rect r, int d);

/* ------------------------------------------------------------------ */
/* Grafik (gfx.c)                                                      */
/* ------------------------------------------------------------------ */

Surf surf_new(int w, int h);
void surf_free(Surf *s);
void surf_resize(Surf *s, int w, int h);
void surf_clip(Surf *s, Rect r);      /* clip = r ∩ yüzey */
void surf_noclip(Surf *s);

uint32_t mix(uint32_t a, uint32_t b, int t);  /* t: 0..255, a->b */
uint32_t lighten(uint32_t c, int amt);         /* amt -255..255 */

void fill_rect(Surf *s, Rect r, uint32_t c);   /* alfa destekli */
void fill_rrect(Surf *s, Rect r, int rad, uint32_t c);
void fill_rrect_vgrad(Surf *s, Rect r, int rad, uint32_t top, uint32_t bot);
void fill_rrect_corners(Surf *s, Rect r, int rad, uint32_t c, int corners); /* bit0 sol-üst, 1 sağ-üst, 2 sol-alt, 3 sağ-alt */
void stroke_rrect(Surf *s, Rect r, int rad, int width, uint32_t c);
void fill_circle(Surf *s, float cx, float cy, float rad, uint32_t c);
void stroke_circle(Surf *s, float cx, float cy, float rad, float width, uint32_t c);
void draw_line(Surf *s, float x0, float y0, float x1, float y1, float width, uint32_t c);
void draw_shadow(Surf *s, Rect r, int rad, int blur, int dy, int alpha);
void blit(Surf *dst, int dx, int dy, const Surf *src, Rect sr);
void blit_rrect(Surf *dst, int dx, int dy, const Surf *src, Rect sr, int rad, int corners);
void blur_region(Surf *s, Rect r, int radius);  /* buzlu cam için kutu bulanıklığı */
void draw_arrow_cursor(Surf *s, int x, int y);
void draw_logo(Surf *s, float cx, float cy, float size, uint32_t c1, uint32_t c2);

/* Simgeler (icons.c) */
typedef enum {
    IC_TERMINAL, IC_FILES, IC_STUDIO, IC_PACKAGES, IC_MONITOR, IC_SETTINGS, IC_ABOUT,
    IC_FOLDER, IC_FILE, IC_AXSFILE, IC_EXEC, IC_PACKAGE, IC_POWER, IC_SEARCH,
    IC_NETWORK, IC_PLAY, IC_SAVE, IC_UP, IC_HOME, IC_REFRESH, IC_NEW, IC_TRASH,
    IC_KEYBOARD, IC_CLOCK, IC_MEMORY, IC_CPU, IC_COUNT
} IconId;
void draw_icon(Surf *s, IconId id, int x, int y, int size);

/* ------------------------------------------------------------------ */
/* Yazı tipi (font.c)                                                  */
/* ------------------------------------------------------------------ */

typedef enum { F_UI, F_UI_BOLD, F_MONO, F_MONO_BOLD, F_COUNT } FontId;

int  font_init(void);
int  text_width(FontId f, int size, const char *s);
int  text_width_n(FontId f, int size, const char *s, int nbytes);
int  font_ascent(FontId f, int size);
int  font_height(FontId f, int size);
int  mono_advance(int size);
/* (x, y) = metnin sol-üst köşesi. Genişliği döndürür. */
int  draw_text(Surf *s, FontId f, int size, int x, int y, uint32_t c, const char *str);
int  draw_text_n(Surf *s, FontId f, int size, int x, int y, uint32_t c, const char *str, int nbytes);
void draw_glyph(Surf *s, FontId f, int size, int x, int y, uint32_t c, uint32_t cp);
/* Genişliği aşarsa sonuna … koyar */
void draw_text_fit(Surf *s, FontId f, int size, int x, int y, int maxw, uint32_t c, const char *str);
void draw_text_center(Surf *s, FontId f, int size, Rect r, uint32_t c, const char *str);
/* Sözcük kaydırmalı metin: en fazla maxlines satır, taşan kısım … ile biter */
void draw_text_wrap(Surf *s, FontId f, int size, int x, int y, int maxw, int maxlines, int lh, uint32_t c, const char *str);

uint32_t utf8_next(const char **p);
int  utf8_prev(const char *s, int pos);
int  utf8_len(uint32_t cp);
int  utf8_put(char *out, uint32_t cp);
/* Türkçe duyarsız arama (ui.c) */
void text_fold(const char *in, char *out, size_t n);
int  text_match(const char *hay, const char *needle);
int  text_prefix(const char *hay, const char *needle);

/* ------------------------------------------------------------------ */
/* Tema                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint32_t base, surface, surface2, overlay, border;
    uint32_t text, subtext, muted;
    uint32_t accent, accent2;
    uint32_t red, yellow, green, blue, orange;
    uint32_t title_focus, title_blur;
} Theme;
extern Theme T;
void theme_set_accent(int idx);
extern const uint32_t ACCENTS[][2];
extern const char *ACCENT_NAMES[];
#define N_ACCENTS 6

/* ------------------------------------------------------------------ */
/* Framebuffer ve giriş                                                */
/* ------------------------------------------------------------------ */

int  fb_open(void);
void fb_close(void);
void fb_present(Surf *back, Rect r);
void fb_flush(void);
extern int SCREEN_W, SCREEN_H;

enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4, MOD_ALTGR = 8, MOD_SUPER = 16 };

typedef struct {
    int code;      /* linux KEY_* */
    uint32_t ch;   /* Unicode karakter (yoksa 0) */
    int mods;
    int down;      /* 1 = basıldı / tekrar, 0 = bırakıldı */
} KeyEv;

typedef enum { M_MOVE, M_DOWN, M_UP, M_WHEEL } MouseKind;
typedef struct {
    MouseKind kind;
    int x, y;      /* ekran ya da pencere içeriği koordinatı */
    int button;    /* 1 sol, 2 sağ, 3 orta */
    int wheel;     /* +1 yukarı, -1 aşağı */
    int clicks;    /* çift tıklama = 2 */
} MouseEv;

int  input_init(void);
void input_rescan(void);
int  input_fds(int *fds, int max);
/* fd hazır olduğunda çağrılır; olayları wm_* işlevlerine iletir */
void input_read(int fd);
extern int kb_layout; /* 0 = TR-Q, 1 = US */
extern const char *LAYOUT_NAMES[];

/* ------------------------------------------------------------------ */
/* Pencere yöneticisi (wm.c)                                           */
/* ------------------------------------------------------------------ */

typedef struct Win Win;
struct AppEntry;
typedef struct App App;

struct App {
    const char *id;
    const char *name;
    const char *desc;
    IconId icon;
    int w, h;                    /* varsayılan içerik boyutu */
    int single;                  /* yalnızca bir örnek */
    void (*init)(Win *w, const char *arg);
    void (*draw)(Win *w, Surf *s);
    void (*mouse)(Win *w, MouseEv *e);
    void (*key)(Win *w, KeyEv *e);
    void (*tick)(Win *w);        /* ~250 ms'de bir */
    void (*close)(Win *w);
    int  (*fds)(Win *w, int *fds, int max);
    void (*io)(Win *w, int fd);
    void (*resize)(Win *w);
};

#define TITLE_H 40

struct Win {
    int id;
    const App *app;
    void *st;           /* uygulama durumu */
    char title[96];
    Rect r;             /* çerçeve (başlık dahil), ekran koordinatı */
    Surf content;       /* içerik yüzeyi: r.w x (r.h - TITLE_H) */
    int dirty;          /* içerik yeniden çizilmeli */
    int minimized, maximized;
    Rect restore;
    int min_w, min_h;
    int closing;
    uint32_t bg;        /* başlık çubuğu arka planı (0 = tema) */
    Rect dmg;           /* çizimde değişen içerik bölgesi (boşsa tamamı) */
    Surf chrome;        /* önbelleğe alınmış başlık çubuğu */
    uint32_t chrome_key;/* önbelleğin hangi duruma ait olduğu */
    char app_name[64];  /* üst çubuk/dock'ta görünen ad (dış uygulamalar için) */
    const struct AppEntry *entry; /* uygulama kaydı (varsa) */
};

/* ------------------------------------------------------------------ */
/* Uygulama kaydı (apps.c): yerleşik + /usr/share/axsde/apps/ *.app     */
/* ------------------------------------------------------------------ */

typedef struct AppEntry {
    char id[64], name[64], desc[160], category[32];
    char exec[256], icon[256], exts[64];   /* exts: "png,jpg" gibi açabildiği dosyalar */
    const App *builtin;                    /* yerleşikse */
} AppEntry;

#define APPS_DIR "/usr/share/axsde/apps"
extern AppEntry APPS[];
extern int n_apps;
void apps_scan(void);
AppEntry *apps_find(const char *id);
AppEntry *apps_for_file(const char *path);
int  app_launch(const AppEntry *a, const char *arg);
void app_draw_icon(Surf *s, const AppEntry *a, int x, int y, int size);
void win_draw_icon(Surf *s, Win *w, int x, int y, int size);
const char *win_app_name(Win *w);

/* PNG simgeler (img.c, stb_image) */
const Surf *icon_png(const char *path, int size);   /* ölçeklenmiş, önbellekli; alfa üst baytta */
int  img_load(const char *path, Surf *out);         /* ARGB (alfa üst baytta); 0 = tamam */
void blit_alpha(Surf *dst, int x, int y, const Surf *src);

/* Dış uygulamalar (ext.c, ADP protokolü) */
extern const App APP_EXTERNAL;
int  ext_init(void);
int  ext_fds(int *fds, int max);
void ext_io(int fd);
Win *wm_open_sized(const App *app, const char *arg, int cw, int ch);
void wm_resize_content(Win *w, int cw, int ch);

extern const App APP_TERMINAL, APP_FILES, APP_STUDIO, APP_PACKAGES,
                 APP_MONITOR, APP_SETTINGS, APP_ABOUT;

Win *wm_open(const App *app, const char *arg);
Win *wm_find(const App *app);
void wm_close(Win *w);
void wm_focus(Win *w);
Win *wm_focused(void);
void wm_damage(Rect r);
void wm_damage_all(void);
void wm_win_dirty(Win *w);
void wm_set_title(Win *w, const char *title);
void wm_notify(const char *title, const char *body, IconId icon);
void wm_on_key(KeyEv *e);
void wm_on_mouse(MouseEv *e);
void wm_wallpaper_changed(void);
void wm_quit(int code);
Rect wm_content_rect(Win *w);  /* içerik alanının ekran koordinatı */
void wm_run_in_terminal(const char *cmd, const char *title);
void wm_open_file(const char *path);
extern int wallpaper_idx;
void wallpaper_render(Surf *dst, int idx);
extern const char *WALLPAPER_NAMES[];
#define N_WALLPAPERS 5
extern int tz_offset_min;

/* ------------------------------------------------------------------ */
/* Anında-mod arayüz öğeleri (ui.c)                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int mx, my;          /* içerik koordinatında fare */
    int down;            /* sol tuş basılı */
    int pressed;         /* bu olayda basıldı */
    int released;        /* bu olayda bırakıldı */
    int clicks;
    int wheel;
    int px, py;          /* basıldığı konum */
    Rect hot[96];        /* son çizimde etkileşimli bölgeler */
    int nhot;
    int cur_hot;         /* farenin üzerinde olduğu bölge (-1 yok) */
} UiState;

void ui_begin(UiState *u);                 /* çizime başlarken */
/* Anında-mod uygulamalar için fare olayı: tıklamalar çizim sırasında işlenir */
void ui_dispatch(Win *w, UiState *u, MouseEv *e);
int  ui_move(UiState *u, MouseEv *e);      /* 1 = üzerinde olunan öğe değişti, yeniden çiz */

typedef enum { BTN_NORMAL, BTN_PRIMARY, BTN_DANGER, BTN_GHOST } BtnStyle;

int  ui_button(Surf *s, UiState *u, Rect r, const char *label, BtnStyle st);
int  ui_icon_button(Surf *s, UiState *u, Rect r, IconId ic, const char *tip);
int  ui_hover(UiState *u, Rect r);
int  ui_clicked(UiState *u, Rect r);
void ui_mouse(UiState *u, MouseEv *e);
void ui_end(UiState *u);        /* olay işlendi: tek seferlik bayrakları temizle */
void ui_panel(Surf *s, Rect r, int rad, uint32_t c);
void ui_progress(Surf *s, Rect r, float v, uint32_t c);
void ui_scrollbar(Surf *s, Rect r, int total, int visible, int offset);
void ui_badge(Surf *s, int x, int y, const char *text, uint32_t bg, uint32_t fg);

/* Tek satırlık metin alanı */
typedef struct {
    char buf[512];
    int len, cur;
    int focus;
} TextField;
void tf_set(TextField *t, const char *s);
int  tf_key(TextField *t, KeyEv *e);    /* 1 = değişti, 2 = Enter */
void tf_draw(Surf *s, TextField *t, Rect r, const char *placeholder);

/* ------------------------------------------------------------------ */
/* Alt süreçler (proc.c)                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    pid_t pid;
    int fd;          /* stdout+stderr okuma ucu (-1: bitti) */
    char *out;       /* biriken çıktı */
    size_t len, cap;
    int status;      /* çıkış kodu (bitince) */
    int done;
} Proc;

int  proc_start(Proc *p, char *const argv[]);
int  proc_read(Proc *p);   /* 0 = sürüyor, 1 = bitti */
void proc_free(Proc *p);
int  run_capture(char *const argv[], char *out, size_t n); /* eşzamanlı, kısa komutlar için */

/* ------------------------------------------------------------------ */
/* Terminal öykünücüsü (term.c)                                        */
/* ------------------------------------------------------------------ */

typedef struct Term Term;
Term *term_new(int cols, int rows);
void  term_free(Term *t);
int   term_spawn(Term *t, char *const argv[], const char *cwd);
int   term_fd(Term *t);
int   term_read(Term *t);             /* pty'den oku; 0 = süreç bitti */
void  term_write(Term *t, const char *s, int n);
void  term_resize(Term *t, int cols, int rows);
void  term_key(Term *t, KeyEv *e);
void  term_scroll(Term *t, int lines);
Rect  term_draw(Term *t, Surf *s, Rect r, int focused, int full); /* çizilen bölge */
void  term_kill(Term *t);
int   term_alive(Term *t);
int   term_exit_code(Term *t);
void  term_feed(Term *t, const char *s, int n);   /* doğrudan metin yaz */
void  term_blink(Term *t);
int   term_font_size(void);
void  term_cell_size(int *cw, int *ch);

/* ------------------------------------------------------------------ */
/* Yapılandırma                                                        */
/* ------------------------------------------------------------------ */

void config_load(void);
void config_save(void);
extern int accent_idx;

/* Genel yardımcılar */
long now_ms(void);
void fmt_size(char *out, size_t n, long long bytes);
int  read_file(const char *path, char *buf, size_t n);

#endif
