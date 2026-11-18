/* Task 32: signal-driven asynchronous I/O (SIGIO), no select()/poll(). */
#if defined(__sun)
#define __EXTENSIONS__ 1
#endif
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#if defined(__sun)
#include <sys/file.h>
#define ASYNC_MODE FASYNC
#else
#define ASYNC_MODE O_ASYNC
#endif

#define MAX_CLIENTS 64

static char socket_file[sizeof(((struct sockaddr_un *)0)->sun_path)];
static int bound = 0;
static volatile sig_atomic_t stopping = 0;
static volatile sig_atomic_t io_ready = 1;

static void on_io(int signo)
{
    (void)signo;
    io_ready = 1; /* Only an async-signal-safe flag update in the handler. */
}
static void on_stop(int signo)
{
    (void)signo;
    stopping = 1;
}
static void cleanup(void)
{
    if (bound)
        unlink(socket_file);
}
static int enable_async(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    if (fcntl(fd, F_SETOWN, getpid()) == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK | ASYNC_MODE);
}
static int write_all(int fd, const char *buf, size_t n)
{
    while (n) {
        ssize_t k = write(fd, buf, n);
        if (k < 0 && errno == EINTR)
            continue;
        if (k <= 0)
            return -1;
        buf += k;
        n -= (size_t)k;
    }
    return 0;
}

/* Check all clients without blocking. Return 1 if something was processed. */
static int scan_clients(int clients[MAX_CLIENTS])
{
    char buf[4096];
    int i, changed = 0;
    ssize_t n, j;

    for (i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] == -1)
            continue;
        n = read(clients[i], buf, sizeof buf);
        if (n > 0) {
            for (j = 0; j < n; ++j)
                buf[j] = (char)toupper((unsigned char)buf[j]);
            if (write_all(STDOUT_FILENO, buf, (size_t)n) == -1) {
                perror("stdout write");
                stopping = 1;
                return changed;
            }
            changed = 1;
        } else if (n == 0) {
            close(clients[i]);
            clients[i] = -1;
            changed = 1;
        } else if (errno == EINTR) {
            changed = 1; /* Retry on the next scan. */
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("client read");
