#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MAX_CLIENTS 64

static char socket_file[sizeof(((struct sockaddr_un *)0)->sun_path)];
static int bound = 0;
static volatile sig_atomic_t stopping = 0;

static void stop_handler(int signo)
{
    (void)signo;
    stopping = 1;
}
static void cleanup(void)
{
    if (bound)
        unlink(socket_file);
}
static int nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags == -1 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
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
static void read_client(struct pollfd *p)
{
    char buf[4096];
    ssize_t n, j;
    for (;;) {
        n = read(p->fd, buf, sizeof buf);
        if (n > 0) {
            for (j = 0; j < n; ++j)
                buf[j] = (char)toupper((unsigned char)buf[j]);
            if (write_all(STDOUT_FILENO, buf, (size_t)n) == -1) {
                perror("stdout write");
                stopping = 1;
                return;
            }
        } else if (n == 0) {
            close(p->fd);
            p->fd = -1;
            return;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        } else {
            perror("client read");
            close(p->fd);
            p->fd = -1;
            return;
        }
    }
}

int main(void)
{
    struct sockaddr_un addr;
    struct sigaction sa;
    struct pollfd p[MAX_CLIENTS + 1];
    int server, client, i, slot, rc;

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (snprintf(socket_file, sizeof socket_file,
                 "/tmp/hw31_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof socket_file) {
        fprintf(stderr, "Socket path too long\n");
        return 1;
    }
    strcpy(addr.sun_path, socket_file);
    server = socket(AF_UNIX, SOCK_STREAM, 0);
