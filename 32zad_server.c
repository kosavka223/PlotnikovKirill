/* Homework 32. Unix domain socket + POSIX AIO (no select/poll).
 * The server prints each byte as soon as an asynchronous read completes.
 * On Solaris: gcc 32zad_server.c -o 32zad_server -lsocket -lnsl -lrt
 */
#if defined(__sun)
#define __EXTENSIONS__ 1
#endif
#define _POSIX_C_SOURCE 200809L

#include <aio.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#if defined(__sun)
#include <sys/file.h>
#define ASYNC_FLAG FASYNC
#else
#define ASYNC_FLAG O_ASYNC
#endif

#define MAX_CLIENTS 64

typedef struct {
    int fd;
    int pending;
    char byte;
    struct aiocb cb;
} Client;

static Client clients[MAX_CLIENTS];
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static volatile sig_atomic_t io_event = 1;
static volatile sig_atomic_t stop_server = 0;
static unsigned long long ones = 0, twos = 0;
static int printed_any = 0;

static void on_io(int sig)
{
    (void)sig;
    io_event = 1;
}

static void on_stop(int sig)
{
    (void)sig;
    stop_server = 1;
}

static void cleanup(void)
{
    unlink(socket_path);
}

/* Put one asynchronous read on a client socket. */
static int start_read(Client *c)
{
    memset(&c->cb, 0, sizeof(c->cb));
    c->cb.aio_fildes = c->fd;
    c->cb.aio_buf = &c->byte;
    c->cb.aio_nbytes = 1;
    c->cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    c->cb.aio_sigevent.sigev_signo = SIGUSR1;

    if (aio_read(&c->cb) == -1) {
        perror("aio_read");
        return -1;
    }
    c->pending = 1;
    return 0;
}

static void forget_client(Client *c)
{
    close(c->fd);
    c->fd = -1;
    c->pending = 0;
}

/* Accept every connection currently waiting. */
static void accept_clients(int listener)
{
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        int i;
        int flags;

        if (fd == -1) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                perror("accept");
            break;
        }

        for (i = 0; i < MAX_CLIENTS && clients[i].fd != -1; ++i)
            ;
        if (i == MAX_CLIENTS) {
            fprintf(stderr, "[SERVER] Too many clients\n");
            close(fd);
            continue;
        }

        /* aio_read uses the connected socket; only the listener needs SIGIO. */
        flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1 ||
            fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
            perror("fcntl client");
            close(fd);
            continue;
        }

        clients[i].fd = fd;
        clients[i].pending = 0;
        if (start_read(&clients[i]) == -1)
            forget_client(&clients[i]);
    }
}

/* Collect all finished AIO operations without blocking on a slow client. */
static void collect_reads(void)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        Client *c = &clients[i];
        int err;
        ssize_t n;
        unsigned char ch;

        if (c->fd == -1 || !c->pending)
            continue;

        err = aio_error(&c->cb);
        if (err == EINPROGRESS)
            continue;

        n = aio_return(&c->cb);
        c->pending = 0;
        if (err != 0 || n <= 0) {
            if (err != 0)
                fprintf(stderr, "\n[SERVER] AIO error: %s\n", strerror(err));
            forget_client(c);
            continue;
        }

        ch = (unsigned char)c->byte;
        ch = (unsigned char)toupper(ch);
        if (ch == '1')
            ++ones;
        if (ch == '2')
            ++twos;

        /* Live output: no imposed alternation between clients. */
        if (write(STDOUT_FILENO, &ch, 1) == -1)
            perror("stdout");
        printed_any = 1;

        if (start_read(c) == -1)
            forget_client(c);
    }

    /* When all clients disconnect, finish the live line and show counts. */
    if (printed_any && (ones != 0 || twos != 0)) {
        int active = 0;
        for (i = 0; i < MAX_CLIENTS; ++i)
            if (clients[i].fd != -1)
                ++active;
        if (active == 0) {
            printf("\n[SERVER] Finished: 1=%llu, 2=%llu\n", ones, twos);
            fflush(stdout);
            ones = 0;
            twos = 0;
            printed_any = 0;
        }
    }
}

static int install_handler(int sig, void (*handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = handler;
    return sigaction(sig, &sa, NULL);
}

int main(void)
{
    struct sockaddr_un addr;
    sigset_t signals, oldmask, waitmask;
    int listener, flags, i;

    for (i = 0; i < MAX_CLIENTS; ++i)
        clients[i].fd = -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (snprintf(socket_path, sizeof(socket_path),
                 "/tmp/hw32_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof(socket_path)) {
        fprintf(stderr, "Socket name is too long\n");
        return 1;
    }
    strcpy(addr.sun_path, socket_path);

    sigemptyset(&signals);
    sigaddset(&signals, SIGIO);
    sigaddset(&signals, SIGUSR1);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &signals, &oldmask) == -1) {
        perror("sigprocmask");
        return 1;
    }

    waitmask = oldmask;
    sigdelset(&waitmask, SIGIO);
    sigdelset(&waitmask, SIGUSR1);
    sigdelset(&waitmask, SIGINT);
    sigdelset(&waitmask, SIGTERM);

    if (install_handler(SIGIO, on_io) == -1 ||
        install_handler(SIGUSR1, on_io) == -1 ||
        install_handler(SIGINT, on_stop) == -1 ||
        install_handler(SIGTERM, on_stop) == -1) {
        perror("sigaction");
        return 1;
    }

    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener == -1) {
        perror("socket");
        return 1;
    }

    /* For this exercise only one instance of the server should run. */
    unlink(socket_path);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1 ||
        listen(listener, MAX_CLIENTS) == -1) {
        perror("bind/listen");
        close(listener);
        return 1;
    }
    if (atexit(cleanup) != 0) {
        close(listener);
        return 1;
    }

    flags = fcntl(listener, F_GETFL, 0);
    if (flags == -1 ||
        fcntl(listener, F_SETOWN, getpid()) == -1 ||
        fcntl(listener, F_SETFL, flags | O_NONBLOCK | ASYNC_FLAG) == -1) {
        perror("fcntl async listener");
        close(listener);
        return 1;
    }

    puts("[SERVER] Waiting for clients. Live incoming characters:");
    fflush(stdout);

    /* Signals are blocked except during sigsuspend(). */
    while (!stop_server) {
        if (io_event) {
            io_event = 0;
            accept_clients(listener);
            collect_reads();
        }
        if (!stop_server && !io_event)
            sigsuspend(&waitmask);
    }

    for (i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd != -1) {
            if (clients[i].pending)
                aio_cancel(clients[i].fd, &clients[i].cb);
            close(clients[i].fd);
        }
    }
    close(listener);
    if (printed_any && (ones != 0 || twos != 0)) {
        putchar('\n');
        printf("[SERVER] Received: 1=%llu, 2=%llu\n", ones, twos);
    }
    sigprocmask(SIG_SETMASK, &oldmask, NULL);
    return 0;
}
