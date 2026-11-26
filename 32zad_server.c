/* Homework 32: Unix domain socket, multiple clients, POSIX AIO.
 * The server reads asynchronously (aio_read), without select/poll.
 * Each received character is printed immediately in a common stream.
 *
 * Solaris: gcc -std=c99 -Wall -Wextra 32zad_server.c -o 32zad_server -lsocket -lnsl -lrt
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
#include <time.h>
#include <unistd.h>

#if defined(__sun)
#include <sys/file.h>
#define SOCKET_ASYNC FASYNC
#else
#define SOCKET_ASYNC O_ASYNC
#endif

#define MAX_CLIENTS 32
#define LINE_WIDTH 80

typedef struct {
    int fd;
    int reading;
    unsigned char byte;
    struct aiocb request;
} Client;

static Client clients[MAX_CLIENTS];
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static volatile sig_atomic_t notified = 1;
static volatile sig_atomic_t stopping = 0;
static unsigned long ones = 0, twos = 0, chars_received = 0;
static unsigned int column = 0;
static struct timespec started_at;
static struct timespec first_byte_at;
static struct timespec last_byte_at;
static int has_bytes = 0;

static double elapsed_seconds(const struct timespec *from,
                              const struct timespec *to)
{
    return (double)(to->tv_sec - from->tv_sec) +
           (double)(to->tv_nsec - from->tv_nsec) / 1000000000.0;
}

static void on_ready(int sig)
{
    (void)sig;
    notified = 1;
}

static void on_stop(int sig)
{
    (void)sig;
    stopping = 1;
}

static void remove_socket(void)
{
    if (socket_path[0])
        unlink(socket_path);
}

static int handler(int signo, void (*fn)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = fn;
    return sigaction(signo, &sa, NULL);
}

static void close_client(Client *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
    c->reading = 0;
}

/* THIS IS THE MAIN ASYNCHRONOUS OPERATION. */
static int queue_read(Client *c)
{
    memset(&c->request, 0, sizeof(c->request));
    c->request.aio_fildes = c->fd;
    c->request.aio_buf = &c->byte;
    c->request.aio_nbytes = 1;
    c->request.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    c->request.aio_sigevent.sigev_signo = SIGUSR1;

    if (aio_read(&c->request) == -1) {
        perror("aio_read");
        return -1;
    }
    c->reading = 1;
    return 0;
}

static void accept_all(int listener)
{
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        int i;

        if (fd < 0) {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK)
                perror("accept");
            return;
        }

        for (i = 0; i < MAX_CLIENTS && clients[i].fd != -1; ++i)
            ;
        if (i == MAX_CLIENTS) {
            fprintf(stderr, "Too many clients\n");
            close(fd);
            continue;
        }

        /* The listening socket is nonblocking; the connected one is used by AIO. */
        {
            int flags = fcntl(fd, F_GETFL, 0);
            if (flags == -1 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == -1) {
                perror("client fcntl");
                close(fd);
                continue;
            }
        }
        clients[i].fd = fd;
        clients[i].reading = 0;
        if (queue_read(&clients[i]) == -1)
            close_client(&clients[i]);
    }
}

static void output_byte(unsigned char ch)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == 0) {
        if (!has_bytes) {
            first_byte_at = now;
            has_bytes = 1;
        }
        last_byte_at = now;
    }

    ch = (unsigned char)toupper(ch);
    if (ch == '1') ++ones;
    if (ch == '2') ++twos;
    ++chars_received;

    /* Do not buffer the output: watch digits arrive live. */
    if (write(STDOUT_FILENO, &ch, 1) < 0)
        perror("stdout");

    if (ch == '\n') {
        column = 0;
    } else if (++column >= LINE_WIDTH) {
        (void)write(STDOUT_FILENO, "\n", 1);
        column = 0;
    }
}

static void collect_completed(void)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; ++i) {
        Client *c = &clients[i];
        int err;
        ssize_t n;

        if (c->fd < 0 || !c->reading)
            continue;

        err = aio_error(&c->request);
        if (err == EINPROGRESS)
            continue;
        if (err < 0) {
            perror("aio_error");
            close_client(c);
            continue;
        }

        n = aio_return(&c->request);
        c->reading = 0;
        if (err != 0 || n <= 0) {
            if (err != 0)
                fprintf(stderr, "AIO error: %s\n", strerror(err));
            close_client(c);
            continue;
        }

        output_byte(c->byte);
        if (queue_read(c) == -1)
            close_client(c);
    }
}

int main(void)
{
    struct sockaddr_un addr;
    sigset_t blocked, original, waitmask;
    int listener, flags, i;
    struct timespec finished_at;

    if (clock_gettime(CLOCK_MONOTONIC, &started_at) != 0) {
        perror("clock_gettime");
        return 1;
    }

    for (i = 0; i < MAX_CLIENTS; ++i)
        clients[i].fd = -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (snprintf(socket_path, sizeof(socket_path),
                 "/tmp/hw32_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof(socket_path)) {
        fprintf(stderr, "Socket path is too long\n");
        return 1;
    }
    strcpy(addr.sun_path, socket_path);

    sigemptyset(&blocked);
    sigaddset(&blocked, SIGIO);
    sigaddset(&blocked, SIGUSR1);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &blocked, &original) == -1) {
        perror("sigprocmask");
        return 1;
    }
    waitmask = original;
    sigdelset(&waitmask, SIGIO);
    sigdelset(&waitmask, SIGUSR1);
    sigdelset(&waitmask, SIGINT);
    sigdelset(&waitmask, SIGTERM);

    if (handler(SIGIO, on_ready) == -1 ||
        handler(SIGUSR1, on_ready) == -1 ||
        handler(SIGINT, on_stop) == -1 ||
        handler(SIGTERM, on_stop) == -1) {
        perror("sigaction");
        return 1;
    }

    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");
        return 1;
    }

    /* Reject another copy of the server; clean only a stale socket file. */
    {
        int probe = socket(AF_UNIX, SOCK_STREAM, 0);
        if (probe >= 0) {
            if (connect(probe, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                fprintf(stderr, "Server already running. Stop it first.\n");
                close(probe);
                close(listener);
                return 1;
            }
            close(probe);
        }
    }
    unlink(socket_path);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == -1 ||
        listen(listener, MAX_CLIENTS) == -1) {
        perror("bind/listen");
        close(listener);
        return 1;
    }
    if (atexit(remove_socket) != 0) {
        close(listener);
        return 1;
    }

    flags = fcntl(listener, F_GETFL, 0);
    if (flags < 0 ||
        fcntl(listener, F_SETOWN, getpid()) == -1 ||
        fcntl(listener, F_SETFL, flags | O_NONBLOCK | SOCKET_ASYNC) == -1) {
        perror("async listener fcntl");
        close(listener);
        return 1;
    }

    puts("[SERVER] AIO: incoming characters (live):");
    fflush(stdout);

    /* No poll() / select(): wake only when SIGIO / SIGUSR1 arrives. */
    while (!stopping) {
        if (notified) {
            notified = 0;
            accept_all(listener);
            collect_completed();
        }
        if (!notified && !stopping)
            sigsuspend(&waitmask);
    }

    for (i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i].fd >= 0) {
            if (clients[i].reading)
                aio_cancel(clients[i].fd, &clients[i].request);
            close_client(&clients[i]);
        }
    }
    close(listener);
    if (column)
        putchar('\n');
    printf("[SERVER] Total: 1=%lu, 2=%lu, all=%lu\n", ones, twos, chars_received);
    if (clock_gettime(CLOCK_MONOTONIC, &finished_at) == 0) {
        printf("[SERVER] Run time: %.2f seconds\n",
               elapsed_seconds(&started_at, &finished_at));
        if (has_bytes)
            printf("[SERVER] Receiving time: %.2f seconds\n",
                   elapsed_seconds(&first_byte_at, &last_byte_at));
    }
    fflush(stdout);
    sigprocmask(SIG_SETMASK, &original, NULL);
    return 0;
}
