/* Task 32: POSIX aio.h version for teacher demonstration.
 * Listening socket: SIGIO. Client I/O: aio_read() + SIGUSR1.
 * No select() or poll(). Solaris: link -lsocket -lnsl -lrt.
 */
#if defined(__sun)
#define __EXTENSIONS__ 1
#endif
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <aio.h>
#include <time.h>
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
#define MAX_LINE 2048

typedef struct {
    int fd;
    long long connected_ns;
    char line[MAX_LINE];
    size_t used;
    struct aiocb aio;
    char chunk[1024];
    int pending;
} Client;

static Client clients[MAX_CLIENTS];
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static long long started_ns;
static volatile sig_atomic_t stopping = 0;
static volatile sig_atomic_t io_ready = 1;
static volatile sig_atomic_t sigio_count = 0;
static volatile sig_atomic_t aio_count = 0;
static int bound = 0;

static long long clock_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static double elapsed_ms(long long from, long long to)
{
    return (double)(to - from) / 1000000.0;
}

static void trace(const char *fmt, ...)
{
    va_list ap;
    printf("[SERVER +%8.1f ms pid=%ld] ",
           elapsed_ms(started_ns, clock_ns()), (long)getpid());
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
}

static void on_sigio(int sig)
{
    (void)sig;
    io_ready = 1;
    ++sigio_count; /* No printf(), malloc(), or clock_gettime() here. */
}

static void on_aio(int sig)
{
    (void)sig;
    io_ready = 1;
    ++aio_count;
}

static void on_stop(int sig)
{
    (void)sig;
    stopping = 1;
}

static void cleanup(void)
{
    if (bound)
        unlink(socket_path);
}

static int make_async(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETOWN, getpid()) == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK | ASYNC_MODE);
}

static void print_message(Client *c, char *line)
{
    long pid;
    long long sent_ns;
    char label[32], message[1500];
    long long received_ns = clock_ns();
    size_t j;

    /* Client protocol: @PID|monotonic_send_ns|ID|message */
    if (sscanf(line, "@%ld|%lld|%31[^|]|%1499[^\r\n]",
               &pid, &sent_ns, label, message) == 4) {
        for (j = 0; message[j] != '\0'; ++j)
            message[j] = (char)toupper((unsigned char)message[j]);
        trace("CLIENT %-7s pid=%-6ld  since-connect=%7.1f ms  "
              "send->recv=%7.3f ms  TEXT: %s",
              label, pid, elapsed_ms(c->connected_ns, received_ns),
              elapsed_ms(sent_ns, received_ns), message);
    } else { /* Old clients remain usable. */
        for (j = 0; line[j] != '\0'; ++j)
            line[j] = (char)toupper((unsigned char)line[j]);
        trace("CLIENT (legacy) TEXT: %s", line);
    }
}

static void consume_data(Client *c, int slot, ssize_t n)
{
    size_t j;
    for (j = 0; j < (size_t)n; ++j) {
        if (c->chunk[j] == '\n') {
            c->line[c->used] = '\0';
            print_message(c, c->line);
            c->used = 0;
        } else if (c->used + 1 < sizeof(c->line)) {
            c->line[c->used++] = c->chunk[j];
        } else {
            trace("WARNING: line too long; resetting slot=%d", slot);
            c->used = 0;
        }
    }
}

static int submit_read(Client *c, int slot)
{
    memset(&c->aio, 0, sizeof(c->aio));
    c->aio.aio_fildes = c->fd;
    c->aio.aio_buf = c->chunk;
    c->aio.aio_nbytes = sizeof(c->chunk);
    c->aio.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    c->aio.aio_sigevent.sigev_signo = SIGUSR1;
    c->aio.aio_sigevent.sigev_value.sival_int = slot;
    if (aio_read(&c->aio) == -1) {
        perror("aio_read");
        return -1;
    }
    c->pending = 1;
    trace("AIO_QUEUED slot=%d fd=%d (continues without waiting)",
          slot, c->fd);
    return 0;
}

static void close_client(Client *c, int slot)
{
    trace("DISCONNECT slot=%d lived=%.1f ms", slot,
          elapsed_ms(c->connected_ns, clock_ns()));
    close(c->fd);
    c->fd = -1;
    c->pending = 0;
    c->used = 0;
}

/* Check ALL asynchronous requests for completion, not one blocking read. */
static void check_completions(void)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        Client *c = &clients[i];
        int status;
        ssize_t n;
        if (c->fd == -1 || !c->pending)
            continue;
        status = aio_error(&c->aio);
        if (status == EINPROGRESS)
            continue;
        n = aio_return(&c->aio);
        c->pending = 0;
        if (status != 0 || n < 0) {
            trace("AIO_ERROR slot=%d error=%s", i, strerror(status));
            close_client(c, i);
        } else if (n == 0) {
            if (c->used) {
                c->line[c->used] = '\0';
                print_message(c, c->line);
            }
            close_client(c, i);
        } else {
            trace("AIO_COMPLETED slot=%d bytes=%ld", i, (long)n);
            consume_data(c, i, n);
            if (submit_read(c, i) == -1)
                close_client(c, i);
        }
    }
}

static void drain_sockets(int listener)
{
    int fd, slot, i, flags;
    for (;;) {
        fd = accept(listener, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                perror("accept");
            break;
        }
        slot = -1;
        for (i = 0; i < MAX_CLIENTS; ++i)
            if (clients[i].fd == -1) { slot = i; break; }
        if (slot < 0) {
            trace("REFUSE: no client slots");
            close(fd);
            continue;
        }
        /* Only listener uses SIGIO. Clients use genuine POSIX aio_read(). */
        flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
            perror("fcntl client");
            close(fd);
            continue;
        }
        clients[slot].fd = fd;
        clients[slot].connected_ns = clock_ns();
        clients[slot].used = 0;
        clients[slot].pending = 0;
        trace("ACCEPT slot=%d fd=%d", slot, fd);
        if (submit_read(&clients[slot], slot) == -1)
            close_client(&clients[slot], slot);
    }
    check_completions();
}

int main(void)
{
    int listener, i;
    struct sockaddr_un addr;
    struct sigaction action;
    sigset_t blocked, oldmask, waitmask;
    sig_atomic_t seen = 0, aio_seen = 0;

    started_ns = clock_ns();
    for (i = 0; i < MAX_CLIENTS; ++i)
        clients[i].fd = -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (snprintf(socket_path, sizeof(socket_path),
                 "/tmp/hw32_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof(socket_path)) {
        fprintf(stderr, "Socket path too long\n");
        return 1;
    }
    strcpy(addr.sun_path, socket_path);

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGIO);
    sigaddset(&blocked, SIGUSR1);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &blocked, &oldmask) == -1) {
        perror("sigprocmask");
        return 1;
    }
    waitmask = oldmask;
    sigdelset(&waitmask, SIGIO);
    sigdelset(&waitmask, SIGUSR1);
    sigdelset(&waitmask, SIGINT);
    sigdelset(&waitmask, SIGTERM);

    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_handler = on_sigio;
    if (sigaction(SIGIO, &action, NULL) == -1) {
        perror("sigaction SIGIO");
        return 1;
    }
    action.sa_handler = on_aio;
    if (sigaction(SIGUSR1, &action, NULL) == -1) {
        perror("sigaction SIGUSR1");
        return 1;
    }
    action.sa_handler = on_stop;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener == -1) {
        perror("socket");
        return 1;
    }
    unlink(socket_path);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1 ||
        listen(listener, MAX_CLIENTS) == -1 || make_async(listener) == -1) {
        perror("bind/listen/fcntl");
        close(listener);
        return 1;
    }
    bound = 1;
    atexit(cleanup);

    trace("START: POSIX aio_read() + SIGUSR1, SIGIO for accept; no poll/select");
    trace("SOCKET: %s; capacity=%d; awaiting clients", socket_path,
          MAX_CLIENTS);
    while (!stopping) {
        if (io_ready) {
            io_ready = 0;
            if (sigio_count != seen) {
                trace("SIGIO received: total=%d (signals can coalesce)",
                      (int)sigio_count);
                seen = sigio_count;
            }
            if (aio_count != aio_seen) {
                trace("AIO completion notifications via SIGUSR1 total=%d",
                      (int)aio_count);
                aio_seen = aio_count;
            }
            drain_sockets(listener);
        }
        if (!stopping)
            sigsuspend(&waitmask);
    }
    trace("STOP; closing sockets");
    for (i = 0; i < MAX_CLIENTS; ++i)
        if (clients[i].fd != -1) {
            if (clients[i].pending)
                aio_cancel(clients[i].fd, &clients[i].aio);
            close(clients[i].fd);
        }
    close(listener);
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    return 0;
}
