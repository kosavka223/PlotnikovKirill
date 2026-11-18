#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

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

int main(void)
{
    int server, client, rc = EXIT_SUCCESS;
    struct sockaddr_un addr;
    struct sigaction sa;
    char buf[4096];
    ssize_t n, i;

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (snprintf(socket_file, sizeof socket_file,
                 "/tmp/hw30_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof socket_file) {
        fprintf(stderr, "Socket path is too long\n");
        return 1;
    }
    strcpy(addr.sun_path, socket_file);

    server = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server == -1) {
        perror("socket");
        return 1;
    }
    /* Remove the socket filename left by a previous interrupted run. */
    unlink(socket_file);
    if (bind(server, (struct sockaddr *)&addr, sizeof addr) == -1 ||
        listen(server, 5) == -1) {
        perror("bind/listen");
        close(server);
        return 1;
    }
    bound = 1;
    atexit(cleanup);

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = stop_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Server 30 listening: %s\n", socket_file);
    fflush(stdout);

    do {
        client = accept(server, NULL, NULL);
    } while (client == -1 && errno == EINTR && !stopping);

    if (client == -1) {
        if (!stopping) {
            perror("accept");
            rc = EXIT_FAILURE;
        }
        close(server);
        return rc;
    }

    while (!stopping) {
        n = read(client, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("read");
            rc = EXIT_FAILURE;
            break;
        }
        if (n == 0)
            break; /* Client disconnected: end both programs. */
        for (i = 0; i < n; ++i)
            buf[i] = (char)toupper((unsigned char)buf[i]);
        if (write_all(STDOUT_FILENO, buf, (size_t)n) == -1) {
            perror("stdout write");
            rc = EXIT_FAILURE;
            break;
        }
    }
    close(client);
    close(server);
    return rc;
}
