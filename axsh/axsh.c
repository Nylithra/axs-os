/*
 * axsh - AxsOS kabuğu
 *
 * Özellikler:
 *   - komut çalıştırma ($PATH araması)
 *   - pipe:          ls | grep x | wc -l
 *   - yönlendirme:   > >> < 2> 2>> 2>&1
 *   - zincirleme:    ; && || ve arka plan &
 *   - tırnak/kaçış:  'tek' "çift $DEGISKEN" \x
 *   - genişletme:    $AD ${AD} $? $$ ~ ve * ? [..] glob
 *   - geçmiş:        ↑/↓ okları, history, !!, !n  (~/.axsh_history dosyasına kaydedilir)
 *   - satır düzenleme: ←/→ Home End Ctrl-A/E/U/K/W/L, Tab ile komut/dosya tamamlama
 *   - yerleşik komutlar: cd pwd exit export unset history source help
 *
 * Kullanım: axsh              etkileşimli
 *           axsh dosya.sh     betik çalıştır
 *           axsh -c "komut"   tek komut
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define AXSH_VERSION "1.0"
#define MAX_LINE     4096
#define HIST_MAX     500

static int last_status;
static int interactive;
static pid_t shell_pgid;
static char hist_path[PATH_MAX];

/* ------------------------------------------------------------------ */
/* Yardımcılar                                                         */
/* ------------------------------------------------------------------ */

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        perror("axsh");
        exit(1);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) {
        perror("axsh");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        perror("axsh");
        exit(1);
    }
    return p;
}

static void err(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("axsh: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* Büyüyen metin tamponu */
typedef struct {
    char *s;
    size_t len, cap;
} Str;

static void str_putc(Str *b, char c)
{
    if (b->len + 2 > b->cap) {
        b->cap = b->cap ? b->cap * 2 : 64;
        b->s = xrealloc(b->s, b->cap);
    }
    b->s[b->len++] = c;
    b->s[b->len] = 0;
}

static void str_puts(Str *b, const char *s)
{
    while (*s)
        str_putc(b, *s++);
}

static char *str_take(Str *b)
{
    if (!b->s)
        return xstrdup("");
    char *s = b->s;
    b->s = NULL;
    b->len = b->cap = 0;
    return s;
}

/* ------------------------------------------------------------------ */
/* Geçmiş                                                              */
/* ------------------------------------------------------------------ */

static char *hist[HIST_MAX];
static int hist_len;
static int hist_base = 1; /* ilk kaydın numarası (history çıktısı için) */

static void hist_add(const char *line)
{
    if (!*line)
        return;
    if (hist_len && strcmp(hist[hist_len - 1], line) == 0)
        return;
    if (hist_len == HIST_MAX) {
        free(hist[0]);
        memmove(hist, hist + 1, sizeof(char *) * (HIST_MAX - 1));
        hist_len--;
        hist_base++;
    }
    hist[hist_len++] = xstrdup(line);
    if (hist_path[0]) {
        FILE *f = fopen(hist_path, "a");
        if (f) {
            fprintf(f, "%s\n", line);
            fclose(f);
        }
    }
}

static void hist_load(void)
{
    const char *home = getenv("HOME");
    if (!home)
        return;
    snprintf(hist_path, sizeof hist_path, "%s/.axsh_history", home);
    FILE *f = fopen(hist_path, "r");
    if (!f)
        return;
    char line[MAX_LINE];
    char saved[PATH_MAX];
    strcpy(saved, hist_path);
    hist_path[0] = 0; /* yüklerken dosyaya tekrar yazma */
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        hist_add(line);
    }
    fclose(f);
    strcpy(hist_path, saved);
}

/* !! ve !n genişletmesi. Değişiklik olduysa yeni satırı döndürür. */
static char *hist_expand(const char *line, int *changed)
{
    Str out = { 0 };
    *changed = 0;
    int sq = 0;
    for (const char *p = line; *p; p++) {
        if (*p == '\'')
            sq = !sq;
        if (!sq && p[0] == '!' && p[1] == '!') {
            if (!hist_len) {
                err("!!: geçmiş boş");
                free(out.s);
                return NULL;
            }
            str_puts(&out, hist[hist_len - 1]);
            p++;
            *changed = 1;
        } else if (!sq && p[0] == '!' && isdigit((unsigned char)p[1])) {
            int n = (int)strtol(p + 1, (char **)&p, 10);
            p--;
            int idx = n - hist_base;
            if (idx < 0 || idx >= hist_len) {
                err("!%d: geçmişte yok", n);
                free(out.s);
                return NULL;
            }
            str_puts(&out, hist[idx]);
            *changed = 1;
        } else {
            str_putc(&out, *p);
        }
    }
    return str_take(&out);
}

/* ------------------------------------------------------------------ */
/* Satır düzenleyici                                                   */
/* ------------------------------------------------------------------ */

static struct termios orig_tio;

static int raw_on(void)
{
    if (tcgetattr(0, &orig_tio) < 0)
        return -1;
    struct termios t = orig_tio;
    t.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(IXON | ICRNL | INPCK | ISTRIP);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    return tcsetattr(0, TCSAFLUSH, &t);
}

static void raw_off(void)
{
    tcsetattr(0, TCSAFLUSH, &orig_tio);
}

static int is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

/* buf içinde [a,b) aralığındaki UTF-8 karakter sayısı (ekran sütunu) */
static int cols(const char *buf, int a, int b)
{
    int n = 0;
    for (int i = a; i < b; i++)
        if (!is_cont((unsigned char)buf[i]))
            n++;
    return n;
}

static void refresh(const char *prompt, const char *buf, int len, int pos)
{
    Str o = { 0 };
    str_puts(&o, "\r");
    str_puts(&o, prompt);
    for (int i = 0; i < len; i++)
        str_putc(&o, buf[i]);
    str_puts(&o, "\033[K");
    int back = cols(buf, pos, len);
    if (back) {
        char tmp[32];
        snprintf(tmp, sizeof tmp, "\033[%dD", back);
        str_puts(&o, tmp);
    }
    if (write(1, o.s, o.len) < 0) { /* yok say */ }
    free(o.s);
}

/* Tab tamamlama: ilk kelimede komut, sonra dosya adı. */
static void complete(char *buf, int *len, int *pos, const char *prompt)
{
    int start = *pos;
    while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '|' &&
           buf[start - 1] != ';' && buf[start - 1] != '&')
        start--;
    int first = 1;
    for (int i = start - 1; i >= 0; i--) {
        if (buf[i] == '|' || buf[i] == ';' || buf[i] == '&')
            break;
        if (buf[i] != ' ') {
            first = 0;
            break;
        }
    }
    char word[PATH_MAX];
    int wl = *pos - start;
    if (wl >= (int)sizeof word)
        return;
    memcpy(word, buf + start, wl);
    word[wl] = 0;

    char **matches = NULL;
    int nm = 0;
    char dir[PATH_MAX] = ".", *pre = word;
    int is_cmd = first && !strchr(word, '/');

    /* aday dizinleri */
    char *dirs[64];
    int nd = 0;
    char *pathcopy = NULL;
    if (is_cmd) {
        const char *path = getenv("PATH");
        pathcopy = xstrdup(path ? path : "/bin");
        for (char *t = strtok(pathcopy, ":"); t && nd < 64; t = strtok(NULL, ":"))
            dirs[nd++] = t;
        static const char *builtins[] = { "cd", "pwd", "exit", "export", "unset",
                                          "history", "source", "help", NULL };
        for (int i = 0; builtins[i]; i++)
            if (strncmp(builtins[i], word, wl) == 0) {
                matches = xrealloc(matches, sizeof(char *) * (nm + 1));
                matches[nm++] = xstrdup(builtins[i]);
            }
    } else {
        char *slash = strrchr(word, '/');
        if (slash) {
            size_t dl = slash - word + 1;
            memcpy(dir, word, dl);
            dir[dl] = 0;
            pre = slash + 1;
        }
        dirs[nd++] = dir;
    }
    size_t pl = strlen(pre);
    for (int d = 0; d < nd; d++) {
        DIR *dp = opendir(dirs[d]);
        if (!dp)
            continue;
        struct dirent *e;
        while ((e = readdir(dp))) {
            if (strncmp(e->d_name, pre, pl) != 0)
                continue;
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            if (e->d_name[0] == '.' && pre[0] != '.')
                continue;
            int dup = 0;
            for (int i = 0; i < nm; i++)
                if (!strcmp(matches[i], e->d_name))
                    dup = 1;
            if (dup)
                continue;
            char full[PATH_MAX * 2];
            snprintf(full, sizeof full, "%s/%s", dirs[d], e->d_name);
            struct stat st;
            int isdir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
            if (is_cmd && (isdir || access(full, X_OK) != 0))
                continue;
            matches = xrealloc(matches, sizeof(char *) * (nm + 1));
            char *m = xmalloc(strlen(e->d_name) + 2);
            sprintf(m, "%s%s", e->d_name, isdir ? "/" : "");
            matches[nm++] = m;
        }
        closedir(dp);
    }
    free(pathcopy);
    if (!nm)
        return;

    /* ortak ön ek */
    size_t common = strlen(matches[0]);
    for (int i = 1; i < nm; i++) {
        size_t j = 0;
        while (j < common && matches[i][j] == matches[0][j])
            j++;
        common = j;
    }
    char ins[PATH_MAX];
    size_t il = common > pl ? common - pl : 0;
    memcpy(ins, matches[0] + pl, il);
    ins[il] = 0;
    if (nm == 1 && ins[il ? il - 1 : 0] != '/' && !(il == 0 && matches[0][strlen(matches[0]) - 1] == '/'))
        strcat(ins, " ");
    il = strlen(ins);

    if (il && *len + (int)il < MAX_LINE - 1) {
        memmove(buf + *pos + il, buf + *pos, *len - *pos);
        memcpy(buf + *pos, ins, il);
        *len += il;
        *pos += il;
        buf[*len] = 0;
    } else if (nm > 1) {
        /* birden fazla aday: listele */
        if (write(1, "\r\n", 2) < 0) { /* yok say */ }
        for (int i = 0; i < nm && i < 100; i++) {
            if (write(1, matches[i], strlen(matches[i])) < 0 || write(1, "  ", 2) < 0) { /* yok say */ }
        }
        if (write(1, "\r\n", 2) < 0) { /* yok say */ }
    }
    for (int i = 0; i < nm; i++)
        free(matches[i]);
    free(matches);
    refresh(prompt, buf, *len, *pos);
}

/* Bir satır oku. EOF'ta NULL döner. */
static char *read_line(const char *prompt)
{
    if (!interactive || raw_on() < 0) {
        if (interactive) {
            fputs(prompt, stdout);
            fflush(stdout);
        }
        char *line = NULL;
        size_t cap = 0;
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) {
            free(line);
            return NULL;
        }
        line[strcspn(line, "\n")] = 0;
        return line;
    }

    char buf[MAX_LINE] = "";
    int len = 0, pos = 0;
    int hidx = hist_len;     /* gezinilen geçmiş konumu */
    char saved[MAX_LINE] = ""; /* ↑'e basmadan önce yazılan */
    refresh(prompt, buf, len, pos);

    for (;;) {
        unsigned char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) {
            if (r < 0 && errno == EINTR)
                continue;
            raw_off();
            return NULL;
        }
        switch (c) {
        case '\r':
        case '\n':
            raw_off();
            if (write(1, "\r\n", 2) < 0) { /* yok say */ }
            return xstrdup(buf);
        case 3: /* Ctrl-C */
            if (write(1, "^C\r\n", 4) < 0) { /* yok say */ }
            len = pos = 0;
            buf[0] = 0;
            hidx = hist_len;
            last_status = 130;
            break;
        case 4: /* Ctrl-D */
            if (len == 0) {
                raw_off();
                if (write(1, "\r\n", 2) < 0) { /* yok say */ }
                return NULL;
            }
            if (pos < len) {
                int n = 1;
                while (pos + n < len && is_cont((unsigned char)buf[pos + n]))
                    n++;
                memmove(buf + pos, buf + pos + n, len - pos - n + 1);
                len -= n;
            }
            break;
        case 127:
        case 8: /* geri sil */
            if (pos > 0) {
                int n = 1;
                while (pos - n > 0 && is_cont((unsigned char)buf[pos - n]))
                    n++;
                memmove(buf + pos - n, buf + pos, len - pos + 1);
                pos -= n;
                len -= n;
            }
            break;
        case 1: pos = 0; break;   /* Ctrl-A */
        case 5: pos = len; break; /* Ctrl-E */
        case 11: /* Ctrl-K */
            len = pos;
            buf[len] = 0;
            break;
        case 21: /* Ctrl-U */
            memmove(buf, buf + pos, len - pos + 1);
            len -= pos;
            pos = 0;
            break;
        case 23: { /* Ctrl-W */
            int s = pos;
            while (s > 0 && buf[s - 1] == ' ')
                s--;
            while (s > 0 && buf[s - 1] != ' ')
                s--;
            memmove(buf + s, buf + pos, len - pos + 1);
            len -= pos - s;
            pos = s;
            break;
        }
        case 12: /* Ctrl-L */
            if (write(1, "\033[H\033[2J", 7) < 0) { /* yok say */ }
            break;
        case '\t':
            complete(buf, &len, &pos, prompt);
            continue;
        case 27: { /* ESC dizisi */
            unsigned char seq[3];
            if (read(0, &seq[0], 1) != 1 || read(0, &seq[1], 1) != 1)
                break;
            if (seq[0] != '[' && seq[0] != 'O')
                break;
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(0, &seq[2], 1) != 1)
                    break;
                if (seq[2] == '~') {
                    if (seq[1] == '3' && pos < len) { /* Delete */
                        int n = 1;
                        while (pos + n < len && is_cont((unsigned char)buf[pos + n]))
                            n++;
                        memmove(buf + pos, buf + pos + n, len - pos - n + 1);
                        len -= n;
                    } else if (seq[1] == '1' || seq[1] == '7') {
                        pos = 0;
                    } else if (seq[1] == '4' || seq[1] == '8') {
                        pos = len;
                    }
                }
                break;
            }
            switch (seq[1]) {
            case 'A': /* ↑ */
            case 'B': /* ↓ */
                if (hidx == hist_len)
                    strcpy(saved, buf);
                if (seq[1] == 'A' && hidx > 0)
                    hidx--;
                else if (seq[1] == 'B' && hidx < hist_len)
                    hidx++;
                else
                    break;
                snprintf(buf, sizeof buf, "%s", hidx == hist_len ? saved : hist[hidx]);
                len = pos = (int)strlen(buf);
                break;
            case 'C': /* → */
                if (pos < len) {
                    pos++;
                    while (pos < len && is_cont((unsigned char)buf[pos]))
                        pos++;
                }
                break;
            case 'D': /* ← */
                if (pos > 0) {
                    pos--;
                    while (pos > 0 && is_cont((unsigned char)buf[pos]))
                        pos--;
                }
                break;
            case 'H': pos = 0; break;
            case 'F': pos = len; break;
            }
            break;
        }
        default:
            if (c >= 32 && len < MAX_LINE - 1) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos++] = (char)c;
                len++;
            }
        }
        refresh(prompt, buf, len, pos);
    }
}

/* ------------------------------------------------------------------ */
/* Sözcük çözümleyici                                                  */
/* ------------------------------------------------------------------ */

enum { T_WORD, T_PIPE, T_AND, T_OR, T_SEMI, T_BG, T_LT, T_GT, T_GTGT,
       T_ERR, T_ERRAPP, T_ERR2OUT, T_END };

typedef struct {
    int type;
    char *text;  /* T_WORD için genişletilmiş metin */
    int glob;    /* tırnaksız glob karakteri içeriyor */
} Tok;

typedef struct {
    Tok *v;
    int n, cap;
} TokList;

static void tok_push(TokList *l, int type, char *text, int glob)
{
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = xrealloc(l->v, sizeof(Tok) * l->cap);
    }
    l->v[l->n++] = (Tok){ type, text, glob };
}

static void tok_free(TokList *l)
{
    for (int i = 0; i < l->n; i++)
        free(l->v[i].text);
    free(l->v);
}

static int is_name_ch(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* $... genişletmesi; p '$' karakterini gösterir, işlenen son karakteri döndürür */
static const char *expand_var(const char *p, Str *out)
{
    p++;
    char name[256];
    int n = 0;
    if (*p == '?') {
        char tmp[16];
        snprintf(tmp, sizeof tmp, "%d", last_status);
        str_puts(out, tmp);
        return p;
    }
    if (*p == '$') {
        char tmp[16];
        snprintf(tmp, sizeof tmp, "%d", (int)getpid());
        str_puts(out, tmp);
        return p;
    }
    if (*p == '{') {
        p++;
        while (*p && *p != '}' && n < 255)
            name[n++] = *p++;
        name[n] = 0;
        if (*p != '}')
            p--;
    } else if (is_name_ch(*p)) {
        while (is_name_ch(*p) && n < 255)
            name[n++] = *p++;
        name[n] = 0;
        p--;
    } else {
        str_putc(out, '$');
        return p - 1;
    }
    const char *v = getenv(name);
    if (v)
        str_puts(out, v);
    return p;
}

/* Ham sözcüğü genişletir: tırnaklar, kaçışlar, $DEGISKEN, ~ . */
static char *expand_word(const char *raw, int *globp)
{
    const char *p = raw;
    Str w = { 0 };
    int glob = 0, any = 0;
    if (*p == '~' && (p[1] == '/' || p[1] == 0 || p[1] == ' ' || p[1] == '\t')) {
        const char *h = getenv("HOME");
        str_puts(&w, h ? h : "/");
        p++;
        any = 1;
    }
    while (*p && !strchr(" \t|&;<>", *p)) {
        any = 1;
        if (*p == '\\' && p[1]) {
            str_putc(&w, p[1]);
            p += 2;
        } else if (*p == '\'') {
            p++;
            while (*p && *p != '\'')
                str_putc(&w, *p++);
            if (!*p) {
                err("kapanmamış tek tırnak");
                free(w.s);
                return NULL;
            }
            p++;
        } else if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1] && strchr("\"\\$`", p[1])) {
                    str_putc(&w, p[1]);
                    p += 2;
                } else if (*p == '$') {
                    p = expand_var(p, &w) + 1;
                } else {
                    str_putc(&w, *p++);
                }
            }
            if (!*p) {
                err("kapanmamış çift tırnak");
                free(w.s);
                return NULL;
            }
            p++;
        } else if (*p == '$') {
            p = expand_var(p, &w) + 1;
        } else {
            if (strchr("*?[", *p))
                glob = 1;
            str_putc(&w, *p++);
        }
    }
    *globp = glob;
    (void)any;
    return str_take(&w);
}

/* Satırı sözcüklere ayırır. Hata olursa -1. */
static int tokenize(const char *line, TokList *toks)
{
    const char *p = line;
    for (;;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p || *p == '#')
            break;
        /* operatörler */
        if (p[0] == '2' && p[1] == '>' && p[2] == '&' && p[3] == '1') {
            tok_push(toks, T_ERR2OUT, NULL, 0); p += 4; continue;
        }
        if (p[0] == '2' && p[1] == '>' && p[2] == '>') {
            tok_push(toks, T_ERRAPP, NULL, 0); p += 3; continue;
        }
        if (p[0] == '2' && p[1] == '>') {
            tok_push(toks, T_ERR, NULL, 0); p += 2; continue;
        }
        if (p[0] == '|' && p[1] == '|') { tok_push(toks, T_OR, NULL, 0); p += 2; continue; }
        if (p[0] == '&' && p[1] == '&') { tok_push(toks, T_AND, NULL, 0); p += 2; continue; }
        if (p[0] == '>' && p[1] == '>') { tok_push(toks, T_GTGT, NULL, 0); p += 2; continue; }
        if (*p == '|') { tok_push(toks, T_PIPE, NULL, 0); p++; continue; }
        if (*p == ';') { tok_push(toks, T_SEMI, NULL, 0); p++; continue; }
        if (*p == '&') { tok_push(toks, T_BG, NULL, 0); p++; continue; }
        if (*p == '<') { tok_push(toks, T_LT, NULL, 0); p++; continue; }
        if (*p == '>') { tok_push(toks, T_GT, NULL, 0); p++; continue; }

        /* sözcük: ham metni al, genişletme çalıştırmadan hemen önce yapılır
         * (böylece "a; echo $?" doğru durumu görür) */
        const char *w0 = p;
        while (*p && !strchr(" \t|&;<>", *p)) {
            if (*p == '\\' && p[1]) {
                p += 2;
            } else if (*p == '\'' || *p == '"') {
                char q = *p++;
                while (*p && *p != q) {
                    if (q == '"' && *p == '\\' && p[1])
                        p++;
                    p++;
                }
                if (!*p) {
                    err("kapanmamış %s tırnak", q == '"' ? "çift" : "tek");
                    return -1;
                }
                p++;
            } else {
                p++;
            }
        }
        tok_push(toks, T_WORD, strndup(w0, p - w0), 0);
    }
    tok_push(toks, T_END, NULL, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Komut yapısı                                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    char **argv;
    int argc;
    char *in, *out, *err;
    int out_app, err_app, err2out;
} Cmd;

typedef struct {
    Cmd *cmds;
    int n;
    int bg;
} Pipeline;

static void cmd_addarg(Cmd *c, char *a)
{
    c->argv = xrealloc(c->argv, sizeof(char *) * (c->argc + 2));
    c->argv[c->argc++] = a;
    c->argv[c->argc] = NULL;
}

static void cmd_addword(Cmd *c, Tok *t)
{
    int isglob = 0;
    char *w = expand_word(t->text, &isglob);
    if (!w)
        w = xstrdup("");
    if (isglob) {
        glob_t g;
        if (glob(w, GLOB_NOCHECK, NULL, &g) == 0) {
            free(w);
            for (size_t i = 0; i < g.gl_pathc; i++)
                cmd_addarg(c, xstrdup(g.gl_pathv[i]));
            globfree(&g);
            return;
        }
    }
    cmd_addarg(c, w);
}

static void pipeline_free(Pipeline *pl)
{
    for (int i = 0; i < pl->n; i++) {
        Cmd *c = &pl->cmds[i];
        for (int j = 0; j < c->argc; j++)
            free(c->argv[j]);
        free(c->argv);
        free(c->in);
        free(c->out);
        free(c->err);
    }
    free(pl->cmds);
    pl->cmds = NULL;
    pl->n = 0;
}

/* toks[*i]'den başlayıp bir pipeline ayrıştırır. */
static int parse_pipeline(TokList *toks, int *i, Pipeline *pl)
{
    memset(pl, 0, sizeof *pl);
    pl->cmds = xmalloc(sizeof(Cmd));
    memset(&pl->cmds[0], 0, sizeof(Cmd));
    pl->n = 1;
    for (;;) {
        Tok *t = &toks->v[*i];
        Cmd *c = &pl->cmds[pl->n - 1];
        switch (t->type) {
        case T_WORD:
            cmd_addword(c, t);
            (*i)++;
            break;
        case T_LT: case T_GT: case T_GTGT: case T_ERR: case T_ERRAPP: {
            Tok *f = &toks->v[*i + 1];
            if (f->type != T_WORD) {
                err("yönlendirmeden sonra dosya adı bekleniyor");
                return -1;
            }
            char **dst = t->type == T_LT ? &c->in :
                         (t->type == T_GT || t->type == T_GTGT) ? &c->out : &c->err;
            free(*dst);
            int g;
            *dst = expand_word(f->text, &g);
            if (!*dst)
                return -1;
            if (t->type == T_GT || t->type == T_GTGT)
                c->out_app = t->type == T_GTGT;
            if (t->type == T_ERR || t->type == T_ERRAPP)
                c->err_app = t->type == T_ERRAPP;
            *i += 2;
            break;
        }
        case T_ERR2OUT:
            c->err2out = 1;
            (*i)++;
            break;
        case T_PIPE:
            if (!c->argc) {
                err("'|' öncesinde komut yok");
                return -1;
            }
            pl->cmds = xrealloc(pl->cmds, sizeof(Cmd) * (pl->n + 1));
            memset(&pl->cmds[pl->n], 0, sizeof(Cmd));
            pl->n++;
            (*i)++;
            break;
        default:
            if (!pl->cmds[pl->n - 1].argc && (pl->n > 1 || pl->cmds[0].in || pl->cmds[0].out)) {
                err("sözdizimi hatası: eksik komut");
                return -1;
            }
            return 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Yerleşik komutlar                                                   */
/* ------------------------------------------------------------------ */

static int run_line(const char *line);
static int run_file(const char *path);

static int bi_cd(char **argv)
{
    const char *dir = argv[1];
    static char prev[PATH_MAX];
    char cur[PATH_MAX];
    if (!getcwd(cur, sizeof cur))
        cur[0] = 0;
    if (!dir)
        dir = getenv("HOME") ? getenv("HOME") : "/";
    else if (!strcmp(dir, "-")) {
        if (!prev[0]) {
            err("cd: önceki dizin yok");
            return 1;
        }
        dir = prev;
        puts(prev);
    }
    char target[PATH_MAX];
    snprintf(target, sizeof target, "%s", dir);
    if (chdir(target) < 0) {
        err("cd: %s: %s", target, strerror(errno));
        return 1;
    }
    snprintf(prev, sizeof prev, "%s", cur);
    char now[PATH_MAX];
    if (getcwd(now, sizeof now))
        setenv("PWD", now, 1);
    setenv("OLDPWD", prev, 1);
    return 0;
}

static int bi_export(char **argv)
{
    if (!argv[1]) {
        extern char **environ;
        for (char **e = environ; *e; e++)
            printf("export %s\n", *e);
        return 0;
    }
    for (int i = 1; argv[i]; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq)
            continue;
        *eq = 0;
        setenv(argv[i], eq + 1, 1);
        *eq = '=';
    }
    return 0;
}

static int bi_history(char **argv)
{
    if (argv[1] && !strcmp(argv[1], "-c")) {
        for (int i = 0; i < hist_len; i++)
            free(hist[i]);
        hist_len = 0;
        if (hist_path[0])
            unlink(hist_path);
        return 0;
    }
    int start = 0;
    if (argv[1])
        start = hist_len - atoi(argv[1]);
    if (start < 0)
        start = 0;
    for (int i = start; i < hist_len; i++)
        printf("%5d  %s\n", i + hist_base, hist[i]);
    return 0;
}

static int bi_help(void)
{
    printf("\033[1maxsh %s\033[0m - AxsOS kabuğu\n\n"
           "Yerleşik komutlar:\n"
           "  cd [dizin|-]      dizin değiştir\n"
           "  pwd               bulunduğun dizin\n"
           "  export AD=deger   ortam değişkeni ata\n"
           "  unset AD          ortam değişkenini sil\n"
           "  history [n|-c]    komut geçmişi (!! son komut, !n n. komut)\n"
           "  source dosya      dosyadaki komutları çalıştır\n"
           "  exit [kod]        kabuktan çık\n\n"
           "Operatörler:  a | b   a > f   a >> f   a < f   a 2> f   a 2>&1\n"
           "              a ; b   a && b   a || b   a &\n\n"
           "Kısayollar:   ↑/↓ geçmiş, Tab tamamlama, Ctrl-A/E başa/sona,\n"
           "              Ctrl-U/K/W silme, Ctrl-L ekranı temizle\n\n"
           "Axs dili:     axs dosya.axs  |  axs (etkileşimli)  |  axs -y (yardım)\n"
           "Paketler:     axpkg list | axpkg install paket.axp | axpkg remove ad\n",
           AXSH_VERSION);
    return 0;
}

/* Yerleşikse çalıştırır ve 1 döner; *st'ye durum yazar. */
static int try_builtin(Cmd *c, int *st)
{
    char **a = c->argv;
    if (!a || !a[0])
        return 0;
    if (!strcmp(a[0], "cd"))           *st = bi_cd(a);
    else if (!strcmp(a[0], "pwd")) {
        char cwd[PATH_MAX];
        *st = getcwd(cwd, sizeof cwd) ? (puts(cwd), 0) : 1;
    }
    else if (!strcmp(a[0], "exit")) {
        exit(a[1] ? atoi(a[1]) : last_status);
    }
    else if (!strcmp(a[0], "export"))  *st = bi_export(a);
    else if (!strcmp(a[0], "unset")) {
        for (int i = 1; a[i]; i++)
            unsetenv(a[i]);
        *st = 0;
    }
    else if (!strcmp(a[0], "history")) *st = bi_history(a);
    else if (!strcmp(a[0], "help"))    *st = bi_help();
    else if (!strcmp(a[0], "source") || !strcmp(a[0], ".")) {
        if (!a[1]) {
            err("source: dosya adı gerekli");
            *st = 1;
        } else {
            *st = run_file(a[1]);
        }
    }
    else if (strchr(a[0], '=') && a[0][0] != '=' && !a[1]) {
        /* AD=deger (yalnız başına): değişken ata */
        char *eq = strchr(a[0], '=');
        *eq = 0;
        setenv(a[0], eq + 1, 1);
        *eq = '=';
        *st = 0;
    }
    else
        return 0;
    fflush(stdout);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Çalıştırma                                                          */
/* ------------------------------------------------------------------ */

static int open_redir(const char *path, int flags, int target)
{
    int fd = open(path, flags, 0644);
    if (fd < 0) {
        err("%s: %s", path, strerror(errno));
        return -1;
    }
    dup2(fd, target);
    close(fd);
    return 0;
}

static int apply_redirs(Cmd *c)
{
    if (c->in && open_redir(c->in, O_RDONLY, 0) < 0)
        return -1;
    if (c->out && open_redir(c->out, O_WRONLY | O_CREAT | (c->out_app ? O_APPEND : O_TRUNC), 1) < 0)
        return -1;
    if (c->err && open_redir(c->err, O_WRONLY | O_CREAT | (c->err_app ? O_APPEND : O_TRUNC), 2) < 0)
        return -1;
    if (c->err2out)
        dup2(1, 2);
    return 0;
}

static void child_signals(void)
{
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_IGN); /* iş kontrolü (fg/bg) yok */
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

static int wait_status(int st)
{
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) {
        int sig = WTERMSIG(st);
        if (sig != SIGINT && sig != SIGPIPE)
            fprintf(stderr, "%s%s\n", strsignal(sig), WCOREDUMP(st) ? " (core dumped)" : "");
        else if (sig == SIGINT)
            fputc('\n', stderr);
        return 128 + sig;
    }
    return 1;
}

static int run_pipeline(Pipeline *pl)
{
    /* Tek yerleşik komut: kabuğun kendi sürecinde (cd'nin çalışması için) */
    if (pl->n == 1 && !pl->bg) {
        Cmd *c = &pl->cmds[0];
        if (!c->argc)
            return 0;
        int saved[3] = { dup(0), dup(1), dup(2) };
        int st = 0;
        int handled = 0;
        if (apply_redirs(c) == 0)
            handled = try_builtin(c, &st);
        else {
            handled = 1;
            st = 1;
        }
        fflush(stdout);
        fflush(stderr);
        for (int k = 0; k < 3; k++) {
            dup2(saved[k], k);
            close(saved[k]);
        }
        if (handled)
            return st;
    }

    pid_t pgid = 0;
    pid_t *pids = xmalloc(sizeof(pid_t) * pl->n);
    int prev_read = -1;
    for (int k = 0; k < pl->n; k++) {
        int fds[2] = { -1, -1 };
        if (k < pl->n - 1 && pipe(fds) < 0) {
            err("pipe: %s", strerror(errno));
            break;
        }
        pid_t pid = fork();
        if (pid < 0) {
            err("fork: %s", strerror(errno));
            break;
        }
        if (pid == 0) {
            if (interactive) {
                setpgid(0, pgid ? pgid : 0);
                if (!pl->bg)
                    tcsetpgrp(0, pgid ? pgid : getpid());
            }
            child_signals();
            if (prev_read >= 0) {
                dup2(prev_read, 0);
                close(prev_read);
            }
            if (fds[1] >= 0) {
                dup2(fds[1], 1);
                close(fds[1]);
                close(fds[0]);
            }
            Cmd *c = &pl->cmds[k];
            if (apply_redirs(c) < 0)
                _exit(1);
            if (!c->argc)
                _exit(0);
            int st;
            if (try_builtin(c, &st))
                _exit(st);
            execvp(c->argv[0], c->argv);
            if (errno == ENOENT)
                err("%s: komut bulunamadı", c->argv[0]);
            else
                err("%s: %s", c->argv[0], strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }
        if (!pgid)
            pgid = pid;
        if (interactive)
            setpgid(pid, pgid);
        pids[k] = pid;
        if (prev_read >= 0)
            close(prev_read);
        if (fds[1] >= 0)
            close(fds[1]);
        prev_read = fds[0];
    }
    if (prev_read >= 0)
        close(prev_read);

    int status = 0;
    if (pl->bg) {
        printf("[%d]\n", (int)pgid);
        free(pids);
        return 0;
    }
    if (interactive)
        tcsetpgrp(0, pgid);
    for (int k = 0; k < pl->n; k++) {
        int st;
        while (waitpid(pids[k], &st, 0) < 0 && errno == EINTR)
            ;
        if (k == pl->n - 1)
            status = wait_status(st);
    }
    if (interactive)
        tcsetpgrp(0, shell_pgid);
    free(pids);
    return status;
}

static int run_tokens(TokList *toks)
{
    int i = 0;
    int skip = 0; /* && / || kısa devresi */
    while (toks->v[i].type != T_END) {
        if (toks->v[i].type == T_SEMI) {
            i++;
            continue;
        }
        Pipeline pl;
        if (parse_pipeline(toks, &i, &pl) < 0) {
            pipeline_free(&pl);
            return last_status = 2;
        }
        int op = toks->v[i].type;
        if (op == T_BG) {
            pl.bg = 1;
            i++;
            op = toks->v[i].type;
        }
        if (!skip)
            last_status = run_pipeline(&pl);
        pipeline_free(&pl);
        if (op == T_AND || op == T_OR) {
            i++;
            skip = (op == T_AND) ? last_status != 0 : last_status == 0;
            if (toks->v[i].type == T_END) {
                err("sözdizimi hatası: %s sonrası komut yok", op == T_AND ? "&&" : "||");
                return last_status = 2;
            }
        } else {
            skip = 0;
        }
    }
    return last_status;
}

static int run_line(const char *line)
{
    TokList toks = { 0 };
    if (tokenize(line, &toks) < 0) {
        tok_free(&toks);
        return last_status = 2;
    }
    int st = run_tokens(&toks);
    tok_free(&toks);
    return st;
}

static int run_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        err("%s: %s", path, strerror(errno));
        return 1;
    }
    char *line = NULL;
    size_t cap = 0;
    int first = 1;
    while (getline(&line, &cap, f) >= 0) {
        line[strcspn(line, "\n")] = 0;
        if (first && line[0] == '#' && line[1] == '!') {
            first = 0;
            continue;
        }
        first = 0;
        run_line(line);
    }
    free(line);
    fclose(f);
    return last_status;
}

/* ------------------------------------------------------------------ */
/* İstem ve ana döngü                                                  */
/* ------------------------------------------------------------------ */

static void make_prompt(char *out, size_t n)
{
    char host[64] = "axsos", cwd[PATH_MAX] = "?";
    gethostname(host, sizeof host);
    if (!getcwd(cwd, sizeof cwd))
        strcpy(cwd, "?");
    const char *home = getenv("HOME");
    char shown[PATH_MAX];
    size_t hl = home ? strlen(home) : 0;
    if (home && hl > 1 && !strncmp(cwd, home, hl) && (cwd[hl] == 0 || cwd[hl] == '/'))
        snprintf(shown, sizeof shown, "~%s", cwd + hl);
    else
        snprintf(shown, sizeof shown, "%s", cwd);
    const char *user = getenv("USER");
    int root = geteuid() == 0;
    snprintf(out, n, "%s\033[1;35m%s@%s\033[0m:\033[1;34m%s\033[0m%s ",
             last_status ? "\033[31m✗\033[0m " : "",
             user ? user : (root ? "root" : "?"), host, shown, root ? "#" : "$");
}

static void reap_bg(void)
{
    int st;
    pid_t pid;
    while ((pid = waitpid(-1, &st, WNOHANG)) > 0)
        if (interactive)
            fprintf(stderr, "[%d] bitti (%d)\n", (int)pid, wait_status(st));
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "-c"))
        return run_line(argv[2]);
    if (argc >= 2 && (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version"))) {
        printf("axsh %s\n", AXSH_VERSION);
        return 0;
    }
    if (argc >= 2 && argv[1][0] != '-')
        return run_file(argv[1]);

    interactive = isatty(0);
    setenv("SHELL", "/bin/axsh", 1);
    if (interactive) {
        /* Ön planda olana kadar bekle, sonra kendi süreç grubumuzu al. */
        while (tcgetpgrp(0) != (shell_pgid = getpgrp()))
            kill(-shell_pgid, SIGTTIN);
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        shell_pgid = getpid();
        if (getpgrp() != shell_pgid)
            setpgid(0, shell_pgid);
        tcsetpgrp(0, shell_pgid);

        hist_load();
        /* Başlangıç dosyaları */
        if (access("/etc/axshrc", R_OK) == 0)
            run_file("/etc/axshrc");
        const char *home = getenv("HOME");
        if (home) {
            char rc[PATH_MAX];
            snprintf(rc, sizeof rc, "%s/.axshrc", home);
            if (access(rc, R_OK) == 0)
                run_file(rc);
        }
        last_status = 0;
    }

    char prompt[PATH_MAX + 128];
    for (;;) {
        reap_bg();
        make_prompt(prompt, sizeof prompt);
        char *line = read_line(prompt);
        if (!line) {
            if (interactive)
                puts("çıkış");
            break;
        }
        int changed = 0;
        char *exp = hist_expand(line, &changed);
        free(line);
        if (!exp)
            continue;
        if (changed && interactive)
            puts(exp);
        char *s = exp;
        while (*s == ' ')
            s++;
        if (interactive && *s)
            hist_add(exp);
        run_line(exp);
        free(exp);
        fflush(stdout);
    }
    return last_status;
}
