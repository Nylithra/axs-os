/* AxsDE - uygulama kaydı: yerleşik uygulamalar + marketten kurulan .app kayıtları */
#define _GNU_SOURCE
#include "axsde.h"

#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_APPS 96
AppEntry APPS[MAX_APPS];
int n_apps;

static void add_builtin(const App *a, const char *cat)
{
    AppEntry *e = &APPS[n_apps++];
    memset(e, 0, sizeof *e);
    snprintf(e->id, sizeof e->id, "%s", a->id);
    snprintf(e->name, sizeof e->name, "%s", a->name);
    snprintf(e->desc, sizeof e->desc, "%s", a->desc);
    snprintf(e->category, sizeof e->category, "%s", cat);
    e->builtin = a;
}

/* .app dosyası: anahtar=değer satırları (name, exec, icon, category, desc, ext) */
static void add_file(const char *path, const char *stem)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    AppEntry e;
    memset(&e, 0, sizeof e);
    snprintf(e.id, sizeof e.id, "%s", stem);
    char line[512];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#')
            continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(line, "name")) snprintf(e.name, sizeof e.name, "%s", v);
        else if (!strcmp(line, "exec")) snprintf(e.exec, sizeof e.exec, "%s", v);
        else if (!strcmp(line, "icon")) snprintf(e.icon, sizeof e.icon, "%s", v);
        else if (!strcmp(line, "category")) snprintf(e.category, sizeof e.category, "%s", v);
        else if (!strcmp(line, "desc")) snprintf(e.desc, sizeof e.desc, "%s", v);
        else if (!strcmp(line, "ext")) snprintf(e.exts, sizeof e.exts, "%s", v);
    }
    fclose(f);
    if (!e.name[0] || !e.exec[0] || n_apps >= MAX_APPS)
        return;
    APPS[n_apps++] = e;
}

void apps_scan(void)
{
    n_apps = 0;
    add_builtin(&APP_TERMINAL, "Sistem");
    add_builtin(&APP_FILES, "Sistem");
    add_builtin(&APP_STUDIO, "Geliştirme");
    add_builtin(&APP_PACKAGES, "Sistem");
    add_builtin(&APP_MONITOR, "Sistem");
    add_builtin(&APP_SETTINGS, "Sistem");
    add_builtin(&APP_ABOUT, "Sistem");
    DIR *d = opendir(APPS_DIR);
    struct dirent *de;
    while (d && (de = readdir(d))) {
        size_t l = strlen(de->d_name);
        if (l < 5 || strcmp(de->d_name + l - 4, ".app"))
            continue;
        char path[512], stem[64];
        snprintf(path, sizeof path, APPS_DIR "/%s", de->d_name);
        snprintf(stem, sizeof stem, "%.*s", (int)(l - 4), de->d_name);
        add_file(path, stem);
    }
    if (d)
        closedir(d);
    /* dış uygulamaları ada göre sırala (yerleşikler başta kalır) */
    for (int i = 8; i < n_apps; i++) {
        AppEntry k = APPS[i];
        int j = i - 1;
        while (j >= 7 && strcasecmp(APPS[j].name, k.name) > 0) {
            APPS[j + 1] = APPS[j];
            j--;
        }
        APPS[j + 1] = k;
    }
}

AppEntry *apps_find(const char *id)
{
    for (int i = 0; i < n_apps; i++)
        if (!strcmp(APPS[i].id, id))
            return &APPS[i];
    return NULL;
}

AppEntry *apps_for_file(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot)
        return NULL;
    dot++;
    for (int i = 0; i < n_apps; i++) {
        if (!APPS[i].exts[0])
            continue;
        char buf[64];
        snprintf(buf, sizeof buf, "%s", APPS[i].exts);
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ","))
            if (!strcasecmp(t, dot))
                return &APPS[i];
    }
    return NULL;
}

int app_launch(const AppEntry *a, const char *arg)
{
    if (a->builtin)
        return wm_open(a->builtin, arg) ? 0 : -1;
    if (access(a->exec, X_OK) != 0) {
        wm_notify(a->name, "Uygulama dosyası bulunamadı", IC_ABOUT);
        return -1;
    }
    /* çift fork: süreç init'e devredilir, zombi kalmaz */
    pid_t p = fork();
    if (p == 0) {
        if (fork() == 0) {
            setsid();
            for (int sig = 1; sig < 32; sig++)
                signal(sig, SIG_DFL);
            int nul = open("/dev/null", O_RDWR);
            if (nul >= 0) {
                dup2(nul, 0);
                char log[128];
                snprintf(log, sizeof log, "/tmp/%s.log", a->id);
                int lf = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                dup2(lf >= 0 ? lf : nul, 1);
                dup2(lf >= 0 ? lf : nul, 2);
            }
            /* masaüstünün açık dosyalarını (DRM, giriş aygıtları, soketler) devretme */
            for (int fd = 3; fd < 1024; fd++)
                close(fd);
            setenv("AXSDE_APP_ID", a->id, 1);
            if (getenv("HOME") && chdir(getenv("HOME")) < 0) { /* yok say */ }
            execl(a->exec, a->exec, arg, (char *)NULL);
            _exit(127);
        }
        _exit(0);
    }
    if (p > 0)
        waitpid(p, NULL, 0);
    return 0;
}

void app_draw_icon(Surf *s, const AppEntry *a, int x, int y, int size)
{
    if (a->builtin) {
        draw_icon(s, a->builtin->icon, x, y, size);
        return;
    }
    const Surf *ic = icon_png(a->icon, size);
    if (ic)
        blit_alpha(s, x, y, ic);
    else
        draw_icon(s, IC_EXEC, x, y, size);
}

void win_draw_icon(Surf *s, Win *w, int x, int y, int size)
{
    if (w->entry)
        app_draw_icon(s, w->entry, x, y, size);
    else
        draw_icon(s, w->app->icon, x, y, size);
}

const char *win_app_name(Win *w)
{
    return w->app_name[0] ? w->app_name : w->app->name;
}
