#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

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

int main(int argc, char **argv)
{
    struct sockaddr_un addr;
    int sock, i, rc = 0;
    char buf[4096];
    ssize_t n;

    signal(SIGPIPE, SIG_IGN);
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    if (snprintf(addr.sun_path, sizeof addr.sun_path,
                 "/tmp/hw31_%lu.sock", (unsigned long)getuid()) >=
        (int)sizeof addr.sun_path) {
        fprintf(stderr, "Socket path too long\n");
        return 1;
    }
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        return 1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof addr) == -1) {
        perror("connect (start the server first)");
        close(sock);
        return 1;
    }

    if (argc > 1) {
        /* Command-line text, separated by spaces. */
        for (i = 1; i < argc; ++i) {
            if (write_all(sock, argv[i], strlen(argv[i])) == -1 ||
                write_all(sock, i == argc - 1 ? "\n" : " ", 1) == -1) {
                perror("send");
                rc = 1;
                break;
            }
        }
    } else {
        /* Interactive input: Ctrl+D on an empty line ends input. */
        while ((n = read(STDIN_FILENO, buf, sizeof buf)) != 0) {
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                perror("stdin read");
                rc = 1;
                break;
            }
            if (write_all(sock, buf, (size_t)n) == -1) {
                perror("send");
                rc = 1;
                break;
            }
        }
    }
    close(sock); /* The server sees EOF. */
    return rc;
}
