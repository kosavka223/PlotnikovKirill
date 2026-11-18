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
            close(clients[i]);
            clients[i] = -1;
            changed = 1;
        }
    }
    return changed;
}

static void drain_sockets(int server, int clients[MAX_CLIENTS])
{
    int client, i, slot;

    /* Listener is nonblocking: accept all pending connections. */
    for (;;) {
        client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                perror("accept");
            break;
        }
        slot = -1;
        for (i = 0; i < MAX_CLIENTS; ++i)
            if (clients[i] == -1) {
                slot = i;
                break;
            }
        if (slot == -1 || enable_async(client) == -1) {
            fprintf(stderr, "Client limit or async setup error\n");
            close(client);
        } else {
            clients[slot] = client;
        }
    }

    /* Round-robin: at most one chunk per client per pass. */
    while (!stopping && scan_clients(clients))
        ;
}

int main(void)
{
    struct sockaddr_un addr;
    struct sigaction sa;
    sigset_t block, previous, wait_mask;
    int server, clients[MAX_CLIENTS], i;

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (snprintf(socket_file, sizeof socket_file,
                 "/tmp/hw32_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof socket_file) {
        fprintf(stderr, "Socket path too long\n");
        return 1;
    }
    strcpy(addr.sun_path, socket_file);

    /* Block the signals while examining sockets: avoids lost wake-ups. */
    sigemptyset(&block);
    sigaddset(&block, SIGIO);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &block, &previous) == -1) {
        perror("sigprocmask");
        return 1;
    }
    wait_mask = previous;
    sigdelset(&wait_mask, SIGIO);
    sigdelset(&wait_mask, SIGINT);
    sigdelset(&wait_mask, SIGTERM);

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_io;
    if (sigaction(SIGIO, &sa, NULL) == -1) {
        perror("sigaction SIGIO");
        return 1;
    }
    sa.sa_handler = on_stop;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server == -1) {
        perror("socket");
        return 1;
    }
    unlink(socket_file);
    if (bind(server, (struct sockaddr *)&addr, sizeof addr) == -1 ||
        listen(server, MAX_CLIENTS) == -1 || enable_async(server) == -1) {
        perror("bind/listen/enable_async");
        close(server);
        return 1;
    }
    bound = 1;
    atexit(cleanup);
    for (i = 0; i < MAX_CLIENTS; ++i)
        clients[i] = -1;

    printf("Server 32 listening: %s (SIGIO, %d clients)\n",
           socket_file, MAX_CLIENTS);
    fflush(stdout);

    while (!stopping) {
        if (io_ready) {
            io_ready = 0;
            drain_sockets(server, clients);
        }
        if (!stopping)
            sigsuspend(&wait_mask); /* Atomically unblock and await SIGIO. */
    }

    for (i = 0; i < MAX_CLIENTS; ++i)
        if (clients[i] != -1)
            close(clients[i]);
    close(server);
    sigprocmask(SIG_SETMASK, &previous, NULL);
    return 0;
}
