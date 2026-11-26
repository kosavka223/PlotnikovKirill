/* Homework 32 client: send one character at a time with a short random pause.
 * Example: ./32zad_client 1 500
 *          ./32zad_client 2 500
 * Solaris: gcc -std=c99 -Wall -Wextra 32zad_client.c -o 32zad_client -lsocket -lnsl
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static long parse_count(const char *text)
{
    char *end;
    long n;
    errno = 0;
    n = strtol(text, &end, 10);
    if (errno || !*text || *end || n < 1 || n > 1000000) {
        fprintf(stderr, "Count must be between 1 and 1000000\n");
        exit(1);
    }
    return n;
}

static void pause_randomly(void)
{
    struct timespec tm, rest;
    /* Independent client timing: from 1 to 20 milliseconds, as in the example. */
    tm.tv_sec = 0;
    tm.tv_nsec = (1000000L * (1 + rand() % 20));
    while (nanosleep(&tm, &rest) == -1 && errno == EINTR)
        tm = rest;
}

static int send_byte(int fd, char ch)
{
    ssize_t n;
    do {
        n = write(fd, &ch, 1);
    } while (n == -1 && errno == EINTR);
    return n == 1 ? 0 : -1;
}

int main(int argc, char **argv)
{
    struct sockaddr_un addr;
    struct timespec t;
    int fd;
    unsigned int seed;
    long i, count;
    char ch;

    if (argc != 3 || strlen(argv[1]) != 1) {
        fprintf(stderr, "Usage: %s CHARACTER COUNT\n"
                        "Example: %s 1 500\n", argv[0], argv[0]);
        return 1;
    }
    ch = argv[1][0];
    count = parse_count(argv[2]);
    if (ch < 32 || ch > 126) {
        fprintf(stderr, "Use one printable ASCII character\n");
        return 1;
    }

    if (clock_gettime(CLOCK_REALTIME, &t) == -1)
        t.tv_nsec = 0;
    seed = (unsigned int)(t.tv_nsec ^ (unsigned long)getpid());
    srand(seed);
    signal(SIGPIPE, SIG_IGN);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (snprintf(addr.sun_path, sizeof(addr.sun_path),
                 "/tmp/hw32_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof(addr.sun_path)) {
        fprintf(stderr, "Socket path too long\n");
        return 1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect (start server first)");
        close(fd);
        return 1;
    }

    for (i = 0; i < count; ++i) {
        if (send_byte(fd, ch) == -1) {
            perror("write");
            close(fd);
            return 1;
        }
        if (i + 1 < count)
            pause_randomly();
    }

    close(fd);
    return 0;
}
