/* AxsDE - evdev giriş: klavye (TR-Q / US), fare (göreli) ve tablet (mutlak) */
#include "axsde.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int kb_layout = 0;
const char *LAYOUT_NAMES[] = { "Türkçe Q", "İngilizce (ABD)" };

#define MAX_DEV 16
typedef struct {
    int fd;
    char path[32];
    int abs;                 /* mutlak konum aygıtı */
    int ax_min, ax_max, ay_min, ay_max;
} Dev;
static Dev devs[MAX_DEV];
static int ndev;

static int mods, caps;
static int mouse_x, mouse_y, btn_left, btn_right;
static int pend_move;
static int rel_dx, rel_dy;

#define BITS_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_LONG - 1) / BITS_LONG)
static int test_bit(int bit, const unsigned long *a)
{
    return (a[bit / BITS_LONG] >> (bit % BITS_LONG)) & 1;
}

static int dev_known(const char *path)
{
    for (int i = 0; i < ndev; i++)
        if (!strcmp(devs[i].path, path))
            return 1;
    return 0;
}

void input_rescan(void)
{
    DIR *d = opendir("/dev/input");
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) && ndev < MAX_DEV) {
        if (strncmp(e->d_name, "event", 5))
            continue;
        char path[32];
        snprintf(path, sizeof path, "/dev/input/%.20s", e->d_name);
        if (dev_known(path))
            continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        unsigned long ev[NLONGS(EV_MAX + 1)] = { 0 };
        unsigned long keys[NLONGS(KEY_MAX + 1)] = { 0 };
        unsigned long absb[NLONGS(ABS_MAX + 1)] = { 0 };
        unsigned long relb[NLONGS(REL_MAX + 1)] = { 0 };
        ioctl(fd, EVIOCGBIT(0, sizeof ev), ev);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof absb), absb);
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof relb), relb);
        int is_kbd = test_bit(EV_KEY, ev) && test_bit(KEY_A, keys);
        int is_rel = test_bit(EV_REL, ev) && test_bit(REL_X, relb);
        int is_abs = test_bit(EV_ABS, ev) && test_bit(ABS_X, absb) && test_bit(ABS_Y, absb);
        if (!is_kbd && !is_rel && !is_abs) {
            close(fd);
            continue;
        }
        Dev *dv = &devs[ndev++];
        memset(dv, 0, sizeof *dv);
        dv->fd = fd;
        snprintf(dv->path, sizeof dv->path, "%s", path);
        if (is_abs) {
            struct input_absinfo ai;
            dv->abs = 1;
            if (ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0) {
                dv->ax_min = ai.minimum;
                dv->ax_max = ai.maximum;
            }
            if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0) {
                dv->ay_min = ai.minimum;
                dv->ay_max = ai.maximum;
            }
            if (dv->ax_max <= dv->ax_min)
                dv->ax_max = dv->ax_min + 1;
            if (dv->ay_max <= dv->ay_min)
                dv->ay_max = dv->ay_min + 1;
        }
    }
    closedir(d);
}

int input_init(void)
{
    mouse_x = SCREEN_W / 2;
    mouse_y = SCREEN_H / 2;
    input_rescan();
    return ndev ? 0 : -1;
}

int input_fds(int *fds, int max)
{
    int n = 0;
    for (int i = 0; i < ndev && n < max; i++)
        if (devs[i].fd >= 0)
            fds[n++] = devs[i].fd;
    return n;
}

/* ------------------------------------------------------------------ */
/* Klavye düzenleri: [tuş] = { normal, shift, altgr }                  */
/* ------------------------------------------------------------------ */

typedef struct { uint32_t n, s, g; } KeyMap;

static const KeyMap US[KEY_MAX + 1] = {
    [KEY_1] = { '1', '!', 0 }, [KEY_2] = { '2', '@', 0 }, [KEY_3] = { '3', '#', 0 },
    [KEY_4] = { '4', '$', 0 }, [KEY_5] = { '5', '%', 0 }, [KEY_6] = { '6', '^', 0 },
    [KEY_7] = { '7', '&', 0 }, [KEY_8] = { '8', '*', 0 }, [KEY_9] = { '9', '(', 0 },
    [KEY_0] = { '0', ')', 0 }, [KEY_MINUS] = { '-', '_', 0 }, [KEY_EQUAL] = { '=', '+', 0 },
    [KEY_Q] = { 'q', 'Q', 0 }, [KEY_W] = { 'w', 'W', 0 }, [KEY_E] = { 'e', 'E', 0 },
    [KEY_R] = { 'r', 'R', 0 }, [KEY_T] = { 't', 'T', 0 }, [KEY_Y] = { 'y', 'Y', 0 },
    [KEY_U] = { 'u', 'U', 0 }, [KEY_I] = { 'i', 'I', 0 }, [KEY_O] = { 'o', 'O', 0 },
    [KEY_P] = { 'p', 'P', 0 }, [KEY_LEFTBRACE] = { '[', '{', 0 }, [KEY_RIGHTBRACE] = { ']', '}', 0 },
    [KEY_A] = { 'a', 'A', 0 }, [KEY_S] = { 's', 'S', 0 }, [KEY_D] = { 'd', 'D', 0 },
    [KEY_F] = { 'f', 'F', 0 }, [KEY_G] = { 'g', 'G', 0 }, [KEY_H] = { 'h', 'H', 0 },
    [KEY_J] = { 'j', 'J', 0 }, [KEY_K] = { 'k', 'K', 0 }, [KEY_L] = { 'l', 'L', 0 },
    [KEY_SEMICOLON] = { ';', ':', 0 }, [KEY_APOSTROPHE] = { '\'', '"', 0 }, [KEY_GRAVE] = { '`', '~', 0 },
    [KEY_BACKSLASH] = { '\\', '|', 0 }, [KEY_Z] = { 'z', 'Z', 0 }, [KEY_X] = { 'x', 'X', 0 },
    [KEY_C] = { 'c', 'C', 0 }, [KEY_V] = { 'v', 'V', 0 }, [KEY_B] = { 'b', 'B', 0 },
    [KEY_N] = { 'n', 'N', 0 }, [KEY_M] = { 'm', 'M', 0 }, [KEY_COMMA] = { ',', '<', 0 },
    [KEY_DOT] = { '.', '>', 0 }, [KEY_SLASH] = { '/', '?', 0 }, [KEY_SPACE] = { ' ', ' ', 0 },
    [KEY_102ND] = { '\\', '|', 0 },
    [KEY_KP0] = { '0', '0', 0 }, [KEY_KP1] = { '1', '1', 0 }, [KEY_KP2] = { '2', '2', 0 },
    [KEY_KP3] = { '3', '3', 0 }, [KEY_KP4] = { '4', '4', 0 }, [KEY_KP5] = { '5', '5', 0 },
    [KEY_KP6] = { '6', '6', 0 }, [KEY_KP7] = { '7', '7', 0 }, [KEY_KP8] = { '8', '8', 0 },
    [KEY_KP9] = { '9', '9', 0 }, [KEY_KPDOT] = { '.', '.', 0 }, [KEY_KPPLUS] = { '+', '+', 0 },
    [KEY_KPMINUS] = { '-', '-', 0 }, [KEY_KPASTERISK] = { '*', '*', 0 }, [KEY_KPSLASH] = { '/', '/', 0 },
};

static const KeyMap TR[KEY_MAX + 1] = {
    [KEY_GRAVE] = { '"', 0xE9, '<' },
    [KEY_1] = { '1', '!', '>' }, [KEY_2] = { '2', '\'', 0xA3 }, [KEY_3] = { '3', '^', '#' },
    [KEY_4] = { '4', '+', '$' }, [KEY_5] = { '5', '%', 0xBD }, [KEY_6] = { '6', '&', 0 },
    [KEY_7] = { '7', '/', '{' }, [KEY_8] = { '8', '(', '[' }, [KEY_9] = { '9', ')', ']' },
    [KEY_0] = { '0', '=', '}' }, [KEY_MINUS] = { '*', '?', '\\' }, [KEY_EQUAL] = { '-', '_', '|' },
    [KEY_Q] = { 'q', 'Q', '@' }, [KEY_W] = { 'w', 'W', 0 }, [KEY_E] = { 'e', 'E', 0x20AC },
    [KEY_R] = { 'r', 'R', 0 }, [KEY_T] = { 't', 'T', 0x20BA }, [KEY_Y] = { 'y', 'Y', 0 },
    [KEY_U] = { 'u', 'U', 0 }, [KEY_I] = { 0x131, 'I', 0 }, [KEY_O] = { 'o', 'O', 0 },
    [KEY_P] = { 'p', 'P', 0 }, [KEY_LEFTBRACE] = { 0x11F, 0x11E, 0xA8 }, [KEY_RIGHTBRACE] = { 0xFC, 0xDC, '~' },
    [KEY_A] = { 'a', 'A', 0xE6 }, [KEY_S] = { 's', 'S', 0xDF }, [KEY_D] = { 'd', 'D', 0 },
    [KEY_F] = { 'f', 'F', 0 }, [KEY_G] = { 'g', 'G', 0 }, [KEY_H] = { 'h', 'H', 0 },
    [KEY_J] = { 'j', 'J', 0 }, [KEY_K] = { 'k', 'K', 0 }, [KEY_L] = { 'l', 'L', 0 },
    [KEY_SEMICOLON] = { 0x15F, 0x15E, 0xB4 }, [KEY_APOSTROPHE] = { 'i', 0x130, 0 },
    [KEY_BACKSLASH] = { ',', ';', '`' }, [KEY_Z] = { 'z', 'Z', 0 }, [KEY_X] = { 'x', 'X', 0 },
    [KEY_C] = { 'c', 'C', 0 }, [KEY_V] = { 'v', 'V', 0 }, [KEY_B] = { 'b', 'B', 0 },
    [KEY_N] = { 'n', 'N', 0 }, [KEY_M] = { 'm', 'M', 0 }, [KEY_COMMA] = { 0xF6, 0xD6, 0 },
    [KEY_DOT] = { 0xE7, 0xC7, 0 }, [KEY_SLASH] = { '.', ':', 0 }, [KEY_SPACE] = { ' ', ' ', 0 },
    [KEY_102ND] = { '<', '>', '|' },
    [KEY_KP0] = { '0', '0', 0 }, [KEY_KP1] = { '1', '1', 0 }, [KEY_KP2] = { '2', '2', 0 },
    [KEY_KP3] = { '3', '3', 0 }, [KEY_KP4] = { '4', '4', 0 }, [KEY_KP5] = { '5', '5', 0 },
    [KEY_KP6] = { '6', '6', 0 }, [KEY_KP7] = { '7', '7', 0 }, [KEY_KP8] = { '8', '8', 0 },
    [KEY_KP9] = { '9', '9', 0 }, [KEY_KPDOT] = { ',', ',', 0 }, [KEY_KPPLUS] = { '+', '+', 0 },
    [KEY_KPMINUS] = { '-', '-', 0 }, [KEY_KPASTERISK] = { '*', '*', 0 }, [KEY_KPSLASH] = { '/', '/', 0 },
};

static int is_letter(int code)
{
    return (code >= KEY_Q && code <= KEY_P) || (code >= KEY_A && code <= KEY_L) ||
           (code >= KEY_Z && code <= KEY_M) ||
           (kb_layout == 0 && (code == KEY_LEFTBRACE || code == KEY_RIGHTBRACE ||
                               code == KEY_SEMICOLON || code == KEY_APOSTROPHE ||
                               code == KEY_COMMA || code == KEY_DOT));
}

static uint32_t key_char(int code)
{
    if (code < 0 || code > KEY_MAX)
        return 0;
    const KeyMap *m = kb_layout == 0 ? &TR[code] : &US[code];
    if (mods & MOD_ALTGR)
        return m->g;
    if (mods & MOD_CTRL)
        return 0;
    int shift = (mods & MOD_SHIFT) != 0;
    if (caps && is_letter(code))
        shift = !shift;
    return shift ? m->s : m->n;
}

static void on_key(int code, int value)
{
    int bit = 0;
    switch (code) {
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: bit = MOD_SHIFT; break;
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL:   bit = MOD_CTRL; break;
    case KEY_LEFTALT:                        bit = MOD_ALT; break;
    case KEY_RIGHTALT:                       bit = MOD_ALTGR; break;
    case KEY_LEFTMETA: case KEY_RIGHTMETA:   bit = MOD_SUPER; break;
    }
    if (bit) {
        if (value)
            mods |= bit;
        else
            mods &= ~bit;
    }
    if (code == KEY_CAPSLOCK && value == 1)
        caps = !caps;

    if (code == BTN_LEFT || code == BTN_RIGHT || code == BTN_MIDDLE || code == BTN_TOUCH) {
        MouseEv e = { value ? M_DOWN : M_UP, mouse_x, mouse_y,
                      code == BTN_RIGHT ? 2 : code == BTN_MIDDLE ? 3 : 1, 0, 1 };
        if (code == BTN_LEFT || code == BTN_TOUCH)
            btn_left = value;
        if (code == BTN_RIGHT)
            btn_right = value;
        wm_on_mouse(&e);
        return;
    }
    if (code >= BTN_MISC)
        return;
    KeyEv k = { code, value ? key_char(code) : 0, mods, value != 0 };
    wm_on_key(&k);
}

static void flush_move(void)
{
    if (!pend_move)
        return;
    pend_move = 0;
    MouseEv e = { M_MOVE, mouse_x, mouse_y, 0, 0, 0 };
    wm_on_mouse(&e);
}

void input_read(int fd)
{
    Dev *dv = NULL;
    for (int i = 0; i < ndev; i++)
        if (devs[i].fd == fd)
            dv = &devs[i];
    if (!dv)
        return;
    struct input_event evs[64];
    for (;;) {
        ssize_t n = read(fd, evs, sizeof evs);
        if (n <= 0) {
            if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
                /* aygıt çıkarıldı */
                close(dv->fd);
                dv->fd = -1;
                dv->path[0] = 0;
            }
            break;
        }
        int cnt = (int)(n / sizeof evs[0]);
        for (int i = 0; i < cnt; i++) {
            struct input_event *ev = &evs[i];
            switch (ev->type) {
            case EV_KEY:
                flush_move();
                on_key(ev->code, ev->value);
                break;
            case EV_REL:
                if (ev->code == REL_X) {
                    rel_dx += ev->value;
                } else if (ev->code == REL_Y) {
                    rel_dy += ev->value;
                } else if (ev->code == REL_WHEEL) {
                    flush_move();
                    MouseEv e = { M_WHEEL, mouse_x, mouse_y, 0, ev->value > 0 ? 1 : -1, 0 };
                    wm_on_mouse(&e);
                }
                break;
            case EV_ABS:
                if (ev->code == ABS_X) {
                    mouse_x = (int)((long)(ev->value - dv->ax_min) * (SCREEN_W - 1) / (dv->ax_max - dv->ax_min));
                    pend_move = 1;
                } else if (ev->code == ABS_Y) {
                    mouse_y = (int)((long)(ev->value - dv->ay_min) * (SCREEN_H - 1) / (dv->ay_max - dv->ay_min));
                    pend_move = 1;
                }
                break;
            case EV_SYN:
                if (rel_dx || rel_dy) {
                    /* basit ivme: hızlı harekette daha uzağa git */
                    int sp = (rel_dx < 0 ? -rel_dx : rel_dx) + (rel_dy < 0 ? -rel_dy : rel_dy);
                    int mul = sp > 12 ? 2 : 1;
                    mouse_x = clampi(mouse_x + rel_dx * mul, 0, SCREEN_W - 1);
                    mouse_y = clampi(mouse_y + rel_dy * mul, 0, SCREEN_H - 1);
                    rel_dx = rel_dy = 0;
                    pend_move = 1;
                }
                flush_move();
                break;
            }
        }
    }
    (void)btn_right;
}
