/* Homework 32 client.
 * Send 10 ones, one every 500 ms:
 *   ./32zad_client --chars 1 10 500
 * Send 20 twos, one every 500 ms:
 *   ./32zad_client --chars 2 20 500
 * Ordinary text (also needed by the assignment):
 *   ./32zad_client "Hello WoRLd"
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

static int send_all(int fd, const char *s, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, s, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        s += n;
        len -= (size_t)n;
    }
    return 0;
}

static void pause_ms(long ms)
{
    struct timespec t, rest;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&t, &rest) == -1 && errno == EINTR)
        t = rest;
}

static long read_number(const char *s, long min, long max)
{
    char *end;
    long value;
    errno = 0;
    value = strtol(s, &end, 10);
    if (errno || !*s || *end || value < min || value > max) {
        fprintf(stderr, "Invalid number: %s (expected %ld..%ld)\n",
                s, min, max);
        exit(1);
    }
    return value;
}

int main(int argc, char **argv)
{
    struct sockaddr_un addr;
    int fd;
    int stream = 0;
    char ch = 0;
    long count = 0, interval_ms = 0, i;

    if (argc == 5 && strcmp(argv[1], "--chars") == 0) {
        if (strlen(argv[2]) != 1 ||
            (argv[2][0] != '1' && argv[2][0] != '2')) {
            fprintf(stderr, "Use digit 1 or 2\n");
            return 1;
        }
        stream = 1;
        ch = argv[2][0];
        count = read_number(argv[3], 1, 10000);
        interval_ms = read_number(argv[4], 1, 60000);
    } else if (argc != 2) {
        fprintf(stderr,
                "Usage: %s --chars 1|2 COUNT INTERVAL_MS\n"
                "   or: %s \"Hello WoRLd\"\n", argv[0], argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (snprintf(addr.sun_path, sizeof(addr.sun_path),
                 "/tmp/hw32_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof(addr.sun_path)) {
        fprintf(stderr, "Socket name is too long\n");
        return 1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        perror("socket");
        return 1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect (start the server first)");
        close(fd);
        return 1;
    }

    if (stream) {
        for (i = 0; i < count; ++i) {
            if (send_all(fd, &ch, 1) == -1) {
                perror("send");
                close(fd);
                return 1;
            }
            if (i + 1 < count)
                pause_ms(interval_ms);
        }
    } else {
        if (send_all(fd, argv[1], strlen(argv[1])) == -1 ||
            send_all(fd, "\n", 1) == -1) {
            perror("send");
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}
