/* AxsDE - terminal öykünücüsü (VT100/xterm alt kümesi) + pty */
#define _GNU_SOURCE
#include "axsde.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define FONT_SIZE 14
#define SCROLLBACK 1000

enum { A_BOLD = 1, A_DIM = 2, A_ITALIC = 4, A_UNDER = 8, A_INVERSE = 16 };

typedef struct {
    uint32_t ch;
    uint32_t fg, bg; /* 0 = varsayılan */
    uint8_t attr;
} Cell;

struct Term {
    int cols, rows;
    Cell *main, *alt, *scr;     /* scr: etkin ekran */
    Cell *sb;                   /* geri kaydırma halkası: SCROLLBACK x cols */
    int sb_head, sb_count;
    int view;                   /* kaç satır yukarı bakılıyor */
    int cx, cy, wrap;
    int scx, scy;               /* kaydedilmiş imleç */
    int top, bot;               /* kaydırma bölgesi */
    uint32_t fg, bg;
    uint8_t attr;
    int cursor_on, app_keys, autowrap, in_alt;
    int state;
    char csi[64];
    int csi_len;
    uint32_t utf_cp;
    int utf_need;
    uint8_t *dirty;
    int all_dirty;
    int fd;
    pid_t pid;
    int alive, exit_code;
    int blink_on;
};

static const uint32_t PAL16[16] = {
    HEX(0x45475A), HEX(0xF38BA8), HEX(0xA6E3A1), HEX(0xF9E2AF),
    HEX(0x89B4FA), HEX(0xCBA6F7), HEX(0x94E2D5), HEX(0xBAC2DE),
    HEX(0x6C7086), HEX(0xF7A8BF), HEX(0xB9F0B4), HEX(0xFBECC4),
    HEX(0xA6C8FF), HEX(0xDCC3FF), HEX(0xB0F0E6), HEX(0xE6E9F5),
};
#define DEF_FG HEX(0xCDD6F4)
#define DEF_BG HEX(0x13131D)

static uint32_t pal256(int n)
{
    if (n < 16)
        return PAL16[n];
    if (n < 232) {
        n -= 16;
        static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
        return RGB(lv[n / 36], lv[(n / 6) % 6], lv[n % 6]);
    }
    int g = 8 + (n - 232) * 10;
    return RGB(g, g, g);
}

int term_font_size(void) { return FONT_SIZE; }

void term_cell_size(int *cw, int *ch)
{
    *cw = mono_advance(FONT_SIZE);
    *ch = font_height(F_MONO, FONT_SIZE) + 2;
}

static void mark(Term *t, int row)
{
    if (row >= 0 && row < t->rows)
        t->dirty[row] = 1;
}

static void clear_cells(Term *t, Cell *c, int n)
{
    for (int i = 0; i < n; i++)
        c[i] = (Cell){ ' ', 0, t->bg, 0 };
}

Term *term_new(int cols, int rows)
{
    Term *t = calloc(1, sizeof *t);
    t->cols = maxi(cols, 2);
    t->rows = maxi(rows, 2);
    t->main = calloc((size_t)t->cols * t->rows, sizeof(Cell));
    t->alt = calloc((size_t)t->cols * t->rows, sizeof(Cell));
    t->sb = calloc((size_t)t->cols * SCROLLBACK, sizeof(Cell));
    t->dirty = calloc(t->rows, 1);
    t->scr = t->main;
    t->bot = t->rows - 1;
    t->cursor_on = t->autowrap = 1;
    t->fd = -1;
    t->blink_on = 1;
    clear_cells(t, t->main, t->cols * t->rows);
    clear_cells(t, t->alt, t->cols * t->rows);
    t->all_dirty = 1;
    return t;
}

void term_free(Term *t)
{
    if (!t)
        return;
    if (t->fd >= 0)
        close(t->fd);
    if (t->pid > 0 && t->alive) {
        kill(-t->pid, SIGHUP);
        kill(t->pid, SIGKILL);
        waitpid(t->pid, NULL, 0);
    }
    free(t->main);
    free(t->alt);
    free(t->sb);
    free(t->dirty);
    free(t);
}

int term_spawn(Term *t, char *const argv[], const char *cwd)
{
    int m = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (m < 0 || grantpt(m) < 0 || unlockpt(m) < 0)
        return -1;
    char *sn = ptsname(m);
    if (!sn)
        return -1;
    struct winsize ws = { (unsigned short)t->rows, (unsigned short)t->cols, 0, 0 };
    ioctl(m, TIOCSWINSZ, &ws);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        setsid();
        int s = open(sn, O_RDWR);
        if (s < 0)
            _exit(127);
        ioctl(s, TIOCSCTTY, 0);
        dup2(s, 0);
        dup2(s, 1);
        dup2(s, 2);
        if (s > 2)
            close(s);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        for (int sig = 1; sig < 32; sig++)
            signal(sig, SIG_DFL);
        setenv("TERM", "xterm", 1);
        setenv("COLORTERM", "truecolor", 1);
        unsetenv("NO_COLOR");
        if (cwd && chdir(cwd) < 0) { /* yok say */ }
        execvp(argv[0], argv);
        fprintf(stderr, "%s çalıştırılamadı: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    fcntl(m, F_SETFL, O_NONBLOCK);
    t->fd = m;
    t->pid = pid;
    t->alive = 1;
    return 0;
}

int term_fd(Term *t) { return t->alive ? t->fd : -1; }
int term_alive(Term *t) { return t->alive; }
int term_exit_code(Term *t) { return t->exit_code; }

/* ------------------------------------------------------------------ */
/* Ekran işlemleri                                                     */
/* ------------------------------------------------------------------ */

static Cell *row_ptr(Term *t, int y) { return &t->scr[y * t->cols]; }

static void push_scrollback(Term *t, Cell *row)
{
    memcpy(&t->sb[t->sb_head * t->cols], row, sizeof(Cell) * t->cols);
    t->sb_head = (t->sb_head + 1) % SCROLLBACK;
    if (t->sb_count < SCROLLBACK)
        t->sb_count++;
}

static void scroll_up(Term *t, int top, int bot, int n)
{
    for (int k = 0; k < n; k++) {
        if (top == 0 && !t->in_alt)
            push_scrollback(t, row_ptr(t, 0));
        memmove(row_ptr(t, top), row_ptr(t, top + 1), sizeof(Cell) * t->cols * (bot - top));
        clear_cells(t, row_ptr(t, bot), t->cols);
    }
    for (int y = top; y <= bot; y++)
        mark(t, y);
}

static void scroll_down(Term *t, int top, int bot, int n)
{
    for (int k = 0; k < n; k++) {
        memmove(row_ptr(t, top + 1), row_ptr(t, top), sizeof(Cell) * t->cols * (bot - top));
        clear_cells(t, row_ptr(t, top), t->cols);
    }
    for (int y = top; y <= bot; y++)
        mark(t, y);
}

static void newline(Term *t)
{
    if (t->cy == t->bot)
        scroll_up(t, t->top, t->bot, 1);
    else if (t->cy < t->rows - 1)
        t->cy++;
}

static void put_char(Term *t, uint32_t cp)
{
    if (t->wrap) {
        t->wrap = 0;
        t->cx = 0;
        newline(t);
    }
    Cell *c = &row_ptr(t, t->cy)[t->cx];
    *c = (Cell){ cp, t->fg, t->bg, t->attr };
    mark(t, t->cy);
    if (t->cx + 1 >= t->cols) {
        if (t->autowrap)
            t->wrap = 1;
    } else {
        t->cx++;
    }
}

static void erase(Term *t, int y, int x0, int x1)
{
    x0 = clampi(x0, 0, t->cols);
    x1 = clampi(x1, 0, t->cols);
    if (x1 > x0)
        clear_cells(t, &row_ptr(t, y)[x0], x1 - x0);
    mark(t, y);
}

static void set_alt(Term *t, int on)
{
    if (on == t->in_alt)
        return;
    t->in_alt = on;
    t->scr = on ? t->alt : t->main;
    if (on) {
        clear_cells(t, t->alt, t->cols * t->rows);
        t->scx = t->cx;
        t->scy = t->cy;
    } else {
        t->cx = t->scx;
        t->cy = t->scy;
    }
    t->all_dirty = 1;
}

static void reset(Term *t)
{
    t->fg = t->bg = 0;
    t->attr = 0;
    t->cx = t->cy = t->wrap = 0;
    t->top = 0;
    t->bot = t->rows - 1;
    t->cursor_on = t->autowrap = 1;
    t->app_keys = 0;
    set_alt(t, 0);
    clear_cells(t, t->main, t->cols * t->rows);
    t->all_dirty = 1;
}

static void sgr(Term *t, int *p, int n)
{
    if (n == 0) {
        p[0] = 0;
        n = 1;
    }
    for (int i = 0; i < n; i++) {
        int v = p[i];
        if (v == 0) {
            t->fg = t->bg = 0;
            t->attr = 0;
        } else if (v == 1) t->attr |= A_BOLD;
        else if (v == 2) t->attr |= A_DIM;
        else if (v == 3) t->attr |= A_ITALIC;
        else if (v == 4) t->attr |= A_UNDER;
        else if (v == 7) t->attr |= A_INVERSE;
        else if (v == 22) t->attr &= ~(A_BOLD | A_DIM);
        else if (v == 23) t->attr &= ~A_ITALIC;
        else if (v == 24) t->attr &= ~A_UNDER;
        else if (v == 27) t->attr &= ~A_INVERSE;
        else if (v >= 30 && v <= 37) t->fg = PAL16[v - 30];
        else if (v == 39) t->fg = 0;
        else if (v >= 40 && v <= 47) t->bg = PAL16[v - 40];
        else if (v == 49) t->bg = 0;
        else if (v >= 90 && v <= 97) t->fg = PAL16[v - 90 + 8];
        else if (v >= 100 && v <= 107) t->bg = PAL16[v - 100 + 8];
        else if ((v == 38 || v == 48) && i + 1 < n) {
            uint32_t col = 0;
            if (p[i + 1] == 5 && i + 2 < n) {
                col = pal256(clampi(p[i + 2], 0, 255));
                i += 2;
            } else if (p[i + 1] == 2 && i + 4 < n) {
                col = RGB(clampi(p[i + 2], 0, 255), clampi(p[i + 3], 0, 255), clampi(p[i + 4], 0, 255));
                i += 4;
            } else {
                break;
            }
            if (v == 38)
                t->fg = col;
            else
                t->bg = col;
        }
    }
}

static void reply(Term *t, const char *s)
{
    if (t->fd >= 0 && write(t->fd, s, strlen(s)) < 0) { /* yok say */ }
}

static void do_csi(Term *t, char final)
{
    int priv = t->csi[0] == '?';
    int p[16] = { 0 }, n = 0;
    const char *s = t->csi + (priv ? 1 : 0);
    while (*s && n < 16) {
        if (*s >= '0' && *s <= '9') {
            p[n] = p[n] * 10 + (*s - '0');
        } else if (*s == ';' || *s == ':') {
            n++;
        }
        s++;
    }
    if (t->csi_len > (priv ? 1 : 0))
        n++;
    int a = n > 0 && p[0] ? p[0] : 1;
    t->wrap = 0;
    switch (final) {
    case 'A': t->cy = maxi(t->cy - a, t->cy >= t->top ? t->top : 0); break;
    case 'B': case 'e': t->cy = mini(t->cy + a, t->cy <= t->bot ? t->bot : t->rows - 1); break;
    case 'C': case 'a': t->cx = mini(t->cx + a, t->cols - 1); break;
    case 'D': t->cx = maxi(t->cx - a, 0); break;
    case 'E': t->cx = 0; t->cy = mini(t->cy + a, t->rows - 1); break;
    case 'F': t->cx = 0; t->cy = maxi(t->cy - a, 0); break;
    case 'G': case '`': t->cx = clampi(a - 1, 0, t->cols - 1); break;
    case 'd': t->cy = clampi(a - 1, 0, t->rows - 1); break;
    case 'H': case 'f':
        t->cy = clampi((n > 0 && p[0] ? p[0] : 1) - 1, 0, t->rows - 1);
        t->cx = clampi((n > 1 && p[1] ? p[1] : 1) - 1, 0, t->cols - 1);
        break;
    case 'J': {
        int m = n ? p[0] : 0;
        if (m == 0) {
            erase(t, t->cy, t->cx, t->cols);
            for (int y = t->cy + 1; y < t->rows; y++)
                erase(t, y, 0, t->cols);
        } else if (m == 1) {
            erase(t, t->cy, 0, t->cx + 1);
            for (int y = 0; y < t->cy; y++)
                erase(t, y, 0, t->cols);
        } else {
            for (int y = 0; y < t->rows; y++)
                erase(t, y, 0, t->cols);
        }
        break;
    }
    case 'K': {
        int m = n ? p[0] : 0;
        if (m == 0) erase(t, t->cy, t->cx, t->cols);
        else if (m == 1) erase(t, t->cy, 0, t->cx + 1);
        else erase(t, t->cy, 0, t->cols);
        break;
    }
    case 'L':
        if (t->cy >= t->top && t->cy <= t->bot)
            scroll_down(t, t->cy, t->bot, mini(a, t->bot - t->cy + 1));
        break;
    case 'M':
        if (t->cy >= t->top && t->cy <= t->bot) {
            int save_top = t->top;
            int in_alt = t->in_alt;
            t->in_alt = 1; /* silinen satırlar geri kaydırmaya gitmesin */
            scroll_up(t, t->cy, t->bot, mini(a, t->bot - t->cy + 1));
            t->in_alt = in_alt;
            t->top = save_top;
        }
        break;
    case 'P': {
        Cell *r = row_ptr(t, t->cy);
        a = mini(a, t->cols - t->cx);
        memmove(&r[t->cx], &r[t->cx + a], sizeof(Cell) * (t->cols - t->cx - a));
        clear_cells(t, &r[t->cols - a], a);
        mark(t, t->cy);
        break;
    }
    case '@': {
        Cell *r = row_ptr(t, t->cy);
        a = mini(a, t->cols - t->cx);
        memmove(&r[t->cx + a], &r[t->cx], sizeof(Cell) * (t->cols - t->cx - a));
        clear_cells(t, &r[t->cx], a);
        mark(t, t->cy);
        break;
    }
    case 'X': erase(t, t->cy, t->cx, t->cx + a); break;
    case 'S': scroll_up(t, t->top, t->bot, a); break;
    case 'T': scroll_down(t, t->top, t->bot, a); break;
    case 'm': sgr(t, p, n); break;
    case 'r':
        t->top = clampi((n > 0 && p[0] ? p[0] : 1) - 1, 0, t->rows - 1);
        t->bot = clampi((n > 1 && p[1] ? p[1] : t->rows) - 1, 0, t->rows - 1);
        if (t->top >= t->bot) {
            t->top = 0;
            t->bot = t->rows - 1;
        }
        t->cx = t->cy = 0;
        break;
    case 's': t->scx = t->cx; t->scy = t->cy; break;
    case 'u': t->cx = t->scx; t->cy = t->scy; break;
    case 'n':
        if (p[0] == 6) {
            char b[32];
            snprintf(b, sizeof b, "\033[%d;%dR", t->cy + 1, t->cx + 1);
            reply(t, b);
        } else if (p[0] == 5) {
            reply(t, "\033[0n");
        }
        break;
    case 'c': if (!priv) reply(t, "\033[?6c"); break;
    case 'h':
    case 'l': {
        int on = final == 'h';
        for (int i = 0; i < n; i++) {
            if (!priv)
                continue;
            switch (p[i]) {
            case 1: t->app_keys = on; break;
            case 7: t->autowrap = on; break;
            case 25: t->cursor_on = on; mark(t, t->cy); break;
            case 47: case 1047: set_alt(t, on); break;
            case 1049: set_alt(t, on); if (on) { t->cx = t->cy = 0; } break;
            }
        }
        break;
    }
    }
    mark(t, t->cy);
}

void term_feed(Term *t, const char *buf, int len)
{
    int old_cy = t->cy;
    for (int i = 0; i < len; i++) {
        unsigned char b = (unsigned char)buf[i];
        switch (t->state) {
        case 0: /* normal */
            if (t->utf_need) {
                if ((b & 0xC0) == 0x80) {
                    t->utf_cp = (t->utf_cp << 6) | (b & 0x3F);
                    if (--t->utf_need == 0)
                        put_char(t, t->utf_cp);
                    continue;
                }
                t->utf_need = 0;
                put_char(t, 0xFFFD);
            }
            if (b == 0x1B) {
                t->state = 1;
            } else if (b == '\r') {
                t->cx = 0;
                t->wrap = 0;
            } else if (b == '\n' || b == 0x0B || b == 0x0C) {
                t->wrap = 0;
                newline(t);
            } else if (b == '\b') {
                if (t->cx > 0)
                    t->cx--;
                t->wrap = 0;
            } else if (b == '\t') {
                t->cx = mini((t->cx / 8 + 1) * 8, t->cols - 1);
            } else if (b == 7 || b < 32) {
                /* zil ve diğer denetim karakterleri: yok say */
            } else if (b < 0x80) {
                put_char(t, b);
            } else if ((b & 0xE0) == 0xC0) {
                t->utf_cp = b & 0x1F;
                t->utf_need = 1;
            } else if ((b & 0xF0) == 0xE0) {
                t->utf_cp = b & 0x0F;
                t->utf_need = 2;
            } else if ((b & 0xF8) == 0xF0) {
                t->utf_cp = b & 0x07;
                t->utf_need = 3;
            }
            break;
        case 1: /* ESC */
            t->state = 0;
            switch (b) {
            case '[': t->state = 2; t->csi_len = 0; t->csi[0] = 0; break;
            case ']': t->state = 3; break;
            case '(': case ')': case '*': case '+': t->state = 4; break;
            case '7': t->scx = t->cx; t->scy = t->cy; break;
            case '8': t->cx = t->scx; t->cy = t->scy; break;
            case 'D': newline(t); break;
            case 'E': t->cx = 0; newline(t); break;
            case 'M':
                if (t->cy == t->top)
                    scroll_down(t, t->top, t->bot, 1);
                else if (t->cy > 0)
                    t->cy--;
                break;
            case 'c': reset(t); break;
            default: break;
            }
            break;
        case 2: /* CSI */
            if ((b >= '0' && b <= '9') || b == ';' || b == ':' || b == '?' || b == '>' || b == '=' || b == ' ' || b == '!') {
                if (t->csi_len < (int)sizeof t->csi - 1) {
                    t->csi[t->csi_len++] = (char)b;
                    t->csi[t->csi_len] = 0;
                }
            } else if (b >= 0x40 && b <= 0x7E) {
                if (t->csi[0] != '>' && t->csi[0] != '=')
                    do_csi(t, (char)b);
                t->state = 0;
            } else {
                t->state = 0;
            }
            break;
        case 3: /* OSC: BEL ya da ESC \ ile biter */
            if (b == 7)
                t->state = 0;
            else if (b == 0x1B)
                t->state = 5;
            break;
        case 4: /* karakter kümesi seçimi: bir bayt atla */
            t->state = 0;
            break;
        case 5:
            t->state = 0;
            break;
        }
    }
    mark(t, old_cy);
    mark(t, t->cy);
    if (t->view) {
        t->view = 0;
        t->all_dirty = 1;
    }
}

int term_read(Term *t)
{
    if (!t->alive)
        return 0;
    char buf[8192];
    for (int k = 0; k < 8; k++) {
        ssize_t n = read(t->fd, buf, sizeof buf);
        if (n > 0) {
            term_feed(t, buf, (int)n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return 1;
        /* EIO: çocuk süreç bitti */
        int st = 0;
        waitpid(t->pid, &st, 0);
        t->exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        t->alive = 0;
        close(t->fd);
        t->fd = -1;
        return 0;
    }
    return 1;
}

void term_write(Term *t, const char *s, int n)
{
    if (t->alive && t->fd >= 0 && write(t->fd, s, n) < 0) { /* yok say */ }
}

void term_resize(Term *t, int cols, int rows)
{
    cols = maxi(cols, 2);
    rows = maxi(rows, 2);
    if (cols == t->cols && rows == t->rows)
        return;
    Cell *nm = calloc((size_t)cols * rows, sizeof(Cell));
    Cell *na = calloc((size_t)cols * rows, sizeof(Cell));
    Term tmp = *t;
    tmp.cols = cols;
    clear_cells(&tmp, nm, cols * rows);
    clear_cells(&tmp, na, cols * rows);
    /* İmleç görünür kalsın diye alttan hizala */
    int shift = maxi(0, t->cy - (rows - 1));
    for (int y = 0; y < rows; y++) {
        int sy = y + shift;
        if (sy >= t->rows)
            break;
        int w = mini(cols, t->cols);
        memcpy(&nm[y * cols], &t->main[sy * t->cols], sizeof(Cell) * w);
        memcpy(&na[y * cols], &t->alt[sy * t->cols], sizeof(Cell) * w);
    }
    free(t->main);
    free(t->alt);
    t->main = nm;
    t->alt = na;
    t->scr = t->in_alt ? na : nm;
    free(t->sb);
    t->sb = calloc((size_t)cols * SCROLLBACK, sizeof(Cell));
    t->sb_head = t->sb_count = 0;
    t->view = 0;
    free(t->dirty);
    t->dirty = calloc(rows, 1);
    t->cols = cols;
    t->rows = rows;
    t->cy = clampi(t->cy - shift, 0, rows - 1);
    t->cx = clampi(t->cx, 0, cols - 1);
    t->top = 0;
    t->bot = rows - 1;
    t->all_dirty = 1;
    if (t->fd >= 0) {
        struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
        ioctl(t->fd, TIOCSWINSZ, &ws);
    }
}

void term_scroll(Term *t, int lines)
{
    int nv = clampi(t->view + lines, 0, t->in_alt ? 0 : t->sb_count);
    if (nv != t->view) {
        t->view = nv;
        t->all_dirty = 1;
    }
}

void term_blink(Term *t)
{
    t->blink_on = !t->blink_on;
    mark(t, t->cy);
}

/* ------------------------------------------------------------------ */
/* Klavye                                                              */
/* ------------------------------------------------------------------ */

static char ctrl_letter(int code)
{
    static const char *r1 = "qwertyuiop", *r2 = "asdfghjkl", *r3 = "zxcvbnm";
    if (code >= KEY_Q && code <= KEY_P) return r1[code - KEY_Q];
    if (code >= KEY_A && code <= KEY_L) return r2[code - KEY_A];
    if (code >= KEY_Z && code <= KEY_M) return r3[code - KEY_Z];
    return 0;
}

void term_key(Term *t, KeyEv *e)
{
    if (!e->down)
        return;
    if ((e->mods & MOD_SHIFT) && (e->code == KEY_PAGEUP || e->code == KEY_PAGEDOWN)) {
        term_scroll(t, e->code == KEY_PAGEUP ? t->rows / 2 : -t->rows / 2);
        return;
    }
    const char *seq = NULL;
    char buf[16];
    int ak = t->app_keys;
    switch (e->code) {
    case KEY_ENTER: case KEY_KPENTER: seq = "\r"; break;
    case KEY_BACKSPACE: seq = (e->mods & MOD_CTRL) ? "\027" : "\177"; break;
    case KEY_TAB: seq = (e->mods & MOD_SHIFT) ? "\033[Z" : "\t"; break;
    case KEY_ESC: seq = "\033"; break;
    case KEY_UP: seq = ak ? "\033OA" : "\033[A"; break;
    case KEY_DOWN: seq = ak ? "\033OB" : "\033[B"; break;
    case KEY_RIGHT: seq = (e->mods & MOD_CTRL) ? "\033[1;5C" : ak ? "\033OC" : "\033[C"; break;
    case KEY_LEFT: seq = (e->mods & MOD_CTRL) ? "\033[1;5D" : ak ? "\033OD" : "\033[D"; break;
    case KEY_HOME: seq = "\033[H"; break;
    case KEY_END: seq = "\033[F"; break;
    case KEY_PAGEUP: seq = "\033[5~"; break;
    case KEY_PAGEDOWN: seq = "\033[6~"; break;
    case KEY_DELETE: seq = "\033[3~"; break;
    case KEY_INSERT: seq = "\033[2~"; break;
    case KEY_F1: seq = "\033OP"; break;
    case KEY_F2: seq = "\033OQ"; break;
    case KEY_F3: seq = "\033OR"; break;
    case KEY_F4: seq = "\033OS"; break;
    case KEY_F5: seq = "\033[15~"; break;
    case KEY_F6: seq = "\033[17~"; break;
    case KEY_F7: seq = "\033[18~"; break;
    case KEY_F8: seq = "\033[19~"; break;
    case KEY_F9: seq = "\033[20~"; break;
    case KEY_F10: seq = "\033[21~"; break;
    }
    if (seq) {
        term_write(t, seq, (int)strlen(seq));
    } else if (e->mods & MOD_CTRL) {
        char l = ctrl_letter(e->code);
        if (l) {
            buf[0] = (char)(l - 'a' + 1);
            term_write(t, buf, 1);
        } else if (e->code == KEY_SPACE) {
            term_write(t, "", 1);
        } else if (e->code == KEY_LEFTBRACE) {
            term_write(t, "\033", 1);
        }
    } else if (e->ch) {
        int n = 0;
        if (e->mods & MOD_ALT)
            buf[n++] = 0x1B;
        n += utf8_put(buf + n, e->ch);
        term_write(t, buf, n);
    } else {
        return;
    }
    if (t->view) {
        t->view = 0;
        t->all_dirty = 1;
    }
}

/* ------------------------------------------------------------------ */
/* Çizim                                                               */
/* ------------------------------------------------------------------ */

Rect term_draw(Term *t, Surf *s, Rect r, int focused, int full)
{
    int y0 = -1, y1 = -1;
    int cw, chh;
    term_cell_size(&cw, &chh);
    if (full || t->all_dirty) {
        fill_rect(s, r, DEF_BG);
        memset(t->dirty, 1, t->rows);
        t->all_dirty = 0;
        full = 1;
    }
    int asc_off = 1;
    for (int y = 0; y < t->rows; y++) {
        if (!t->dirty[y])
            continue;
        t->dirty[y] = 0;
        const Cell *row;
        int sy = y - t->view; /* sy < 0: geri kaydırmadan */
        if (sy < 0) {
            int idx = (t->sb_head + SCROLLBACK + sy) % SCROLLBACK;
            if (-sy > t->sb_count)
                continue;
            row = &t->sb[idx * t->cols];
        } else {
            row = &t->scr[sy * t->cols];
        }
        int py = r.y + y * chh;
        if (py >= r.y + r.h)
            break;
        if (y0 < 0)
            y0 = py;
        y1 = py + chh;
        fill_rect(s, (Rect){ r.x, py, r.w, chh }, DEF_BG);
        for (int x = 0; x < t->cols; x++) {
            const Cell *c = &row[x];
            uint32_t fg = c->fg ? c->fg : DEF_FG, bg = c->bg ? c->bg : DEF_BG;
            if (c->attr & A_INVERSE) {
                uint32_t tmp = fg;
                fg = bg;
                bg = tmp;
            }
            int is_cur = focused && t->cursor_on && !t->view && sy == t->cy && x == t->cx && t->blink_on;
            if (is_cur) {
                uint32_t tmp = fg;
                fg = DEF_BG;
                bg = tmp == DEF_BG ? DEF_FG : HEX(0xCDD6F4);
            }
            int px = r.x + x * cw;
            if (bg != DEF_BG)
                fill_rect(s, (Rect){ px, py, cw, chh }, bg);
            if (c->attr & A_DIM)
                fg = mix(fg, DEF_BG, 100);
            if (c->ch > ' ')
                draw_glyph(s, (c->attr & A_BOLD) ? F_MONO_BOLD : F_MONO, FONT_SIZE, px, py + asc_off, fg, c->ch);
            if (c->attr & A_UNDER)
                fill_rect(s, (Rect){ px, py + chh - 2, cw, 1 }, fg);
        }
        /* odak dışıyken içi boş imleç */
        if (!focused && t->cursor_on && !t->view && sy == t->cy)
            stroke_rrect(s, (Rect){ r.x + t->cx * cw, py, cw, chh }, 1, 1, ALPHA(DEF_FG, 160));
    }
    if (full)
        return r;
    if (y0 < 0)
        return (Rect){ 0, 0, 0, 0 };
    return (Rect){ r.x, y0, r.w, y1 - y0 };
}

void term_kill(Term *t)
{
    if (t->alive && t->pid > 0) {
        kill(-t->pid, SIGTERM);
        kill(t->pid, SIGTERM);
    }
}
