/* Task 32 client with a configurable pre-send delay for demonstrations.
 * Usage: ./32zad_client ID DELAY_MS "message"
 * Old syntax ./32zad_client "message" still works.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static long long clock_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == -1) {
        perror("clock_gettime");
        exit(1);
    }
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void pause_ms(long ms)
{
    struct timespec request, leftover;
    request.tv_sec = ms / 1000;
    request.tv_nsec = (ms % 1000) * 1000000L;
    while (nanosleep(&request, &leftover) == -1 && errno == EINTR)
        request = leftover;
}

static int write_all(int fd, const char *s, size_t len)
{
    while (len) {
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

int main(int argc, char **argv)
{
    struct sockaddr_un addr;
    int fd;
    long delay = 0;
    char id[32], packet[1800];
    const char *message;
    long long sent_ns;
    char *end;
    int bytes;

    if (argc == 2) {
        snprintf(id, sizeof(id), "P%ld", (long)getpid());
        message = argv[1];
    } else if (argc == 4) {
        if (strlen(argv[1]) >= sizeof(id) || strchr(argv[1], '|')) {
            fprintf(stderr, "Client ID too long or contains '|'\n");
            return 1;
        }
        strcpy(id, argv[1]);
        errno = 0;
        delay = strtol(argv[2], &end, 10);
        if (errno || *end || delay < 0 || delay > 30000) {
            fprintf(stderr, "DELAY_MS must be 0..30000\n");
            return 1;
        }
        message = argv[3];
    } else {
        fprintf(stderr, "Usage: %s ID DELAY_MS \"message\"\n"
                        "   or: %s \"message\"\n", argv[0], argv[0]);
        return 1;
    }
    if (!*id || strchr(message, '\n') || strchr(message, '\r')) {
        fprintf(stderr, "Use a nonempty ID and a one-line message\n");
        return 1;
    }

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
    if (fd == -1 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("socket/connect (start server first)");
        if (fd != -1)
            close(fd);
        return 1;
    }
    printf("[CLIENT %s] Waiting %.1f s before sending...\n",
           id, delay / 1000.0);
    fflush(stdout);

    pause_ms(delay);  /* Only the CLIENT waits; the SERVER does not sleep. */
    printf("[CLIENT %s] Sending: %s\n", id, message);
    fflush(stdout);
    sent_ns = clock_ns();
    bytes = snprintf(packet, sizeof(packet), "@%ld|%lld|%s|%s\n",
                     (long)getpid(), sent_ns, id, message);
    if (bytes < 0 || bytes >= (int)sizeof(packet)) {
        fprintf(stderr, "Message too long\n");
        close(fd);
        return 1;
    }
    if (write_all(fd, packet, (size_t)bytes) == -1) {
        perror("write");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}
