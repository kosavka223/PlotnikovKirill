/* Task 32 client with a configurable pre-send delay for demonstrations.
 * Usage: ./32zad_client ID DELAY_MS "message"
 * Old syntax ./32zad_client "message" still works.
 * Stream demo: ./32zad_client --stream 1 5000 30
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
    int stream_mode = 0;
    char stream_digit = 0;
    long stream_duration_ms = 0, stream_interval_ms = 30;
    long long stream_until;

    if ((argc == 4 || argc == 5) && strcmp(argv[1], "--stream") == 0) {
        if (strlen(argv[2]) != 1 ||
            (argv[2][0] != '1' && argv[2][0] != '2')) {
            fprintf(stderr, "Stream digit must be 1 or 2\n");
            return 1;
        }
        stream_mode = 1;
        stream_digit = argv[2][0];
        errno = 0;
        stream_duration_ms = strtol(argv[3], &end, 10);
        if (errno || *end || stream_duration_ms < 100 ||
            stream_duration_ms > 60000) {
            fprintf(stderr, "DURATION_MS must be 100..60000\n");
            return 1;
        }
        if (argc == 5) {
            errno = 0;
            stream_interval_ms = strtol(argv[4], &end, 10);
            if (errno || *end || stream_interval_ms < 10 ||
                stream_interval_ms > 1000) {
                fprintf(stderr, "INTERVAL_MS must be 10..1000\n");
                return 1;
            }
        }
        snprintf(id, sizeof(id), "stream-%c", stream_digit);
        message = "";
    } else if (argc == 2) {
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
                        "   or: %s \"message\"\n"
                        "   or: %s --stream 1|2 DURATION_MS [INTERVAL_MS]\n",
                argv[0], argv[0], argv[0]);
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
    if (stream_mode) {
        /* One socket stays open: send many separate 1-byte messages. */
        char header[16];
        int h = snprintf(header, sizeof(header), "@STREAM|%c\n", stream_digit);
        if (h < 0 || h >= (int)sizeof(header) ||
            write_all(fd, header, (size_t)h) < 0) {
            perror("write stream header");
            close(fd);
            return 1;
        }
        stream_until = clock_ns() + (long long)stream_duration_ms * 1000000LL;
        while (clock_ns() < stream_until) {
            if (write_all(fd, &stream_digit, 1) == -1) {
                perror("write stream byte");
                close(fd);
                return 1;
            }
            pause_ms(stream_interval_ms);
        }
        close(fd);
        return 0;
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
