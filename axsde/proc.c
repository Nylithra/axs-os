/* AxsDE - alt süreç çalıştırma (arayüzü dondurmadan) */
#define _GNU_SOURCE
#include "axsde.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void child_setup(int out)
{
    int nul = open("/dev/null", O_RDONLY);
    if (nul >= 0) {
        dup2(nul, 0);
        close(nul);
    }
    dup2(out, 1);
    dup2(out, 2);
    signal(SIGPIPE, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    setenv("NO_COLOR", "1", 1);
}

int proc_start(Proc *p, char *const argv[])
{
    memset(p, 0, sizeof *p);
    p->fd = -1;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0)
        return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        child_setup(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    p->pid = pid;
    p->fd = fds[0];
    return 0;
}

int proc_read(Proc *p)
{
    if (p->done)
        return 1;
    char buf[4096];
    for (;;) {
        ssize_t n = read(p->fd, buf, sizeof buf);
        if (n > 0) {
            if (p->len + n + 1 > p->cap) {
                p->cap = (p->len + n + 1) * 2;
                p->out = realloc(p->out, p->cap);
            }
            memcpy(p->out + p->len, buf, n);
            p->len += n;
            p->out[p->len] = 0;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return 0;
        break; /* EOF */
    }
    close(p->fd);
    p->fd = -1;
    int st = 0;
    waitpid(p->pid, &st, 0);
    p->status = WIFEXITED(st) ? WEXITSTATUS(st) : 128;
    p->done = 1;
    if (!p->out) {
        p->out = calloc(1, 1);
        p->cap = 1;
    }
    return 1;
}

void proc_free(Proc *p)
{
    if (p->fd >= 0) {
        close(p->fd);
        kill(p->pid, SIGTERM);
        waitpid(p->pid, NULL, 0);
    }
    free(p->out);
    memset(p, 0, sizeof *p);
    p->fd = -1;
}

int run_capture(char *const argv[], char *out, size_t n)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0)
        return -1;
    pid_t pid = fork();
    if (pid == 0) {
        child_setup(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    /* Tampon dolsa bile boruyu sonuna kadar oku ki çocuk yazarken takılmasın */
    size_t len = 0;
    char tmp[4096];
    ssize_t r;
    while ((r = read(fds[0], tmp, sizeof tmp)) > 0) {
        size_t c = mini((int)r, (int)(n - 1 - len));
        memcpy(out + len, tmp, c);
        len += c;
    }
    out[len] = 0;
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}
