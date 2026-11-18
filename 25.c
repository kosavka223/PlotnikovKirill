#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int write_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t n = write(fd, data, len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return -1;
        data += n;
        len -= (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int pipefd[2], status, i;
    pid_t pid;

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return 1;
    }
    pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid == 0) {
        char buf[4096];
        ssize_t n, j;
        close(pipefd[1]); /* child only reads */
        while ((n = read(pipefd[0], buf, sizeof buf)) != 0) {
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                perror("child read");
                _exit(1);
            }
            for (j = 0; j < n; ++j)
                buf[j] = (char)toupper((unsigned char)buf[j]);
            if (write_all(STDOUT_FILENO, buf, (size_t)n) == -1) {
                perror("child write");
                _exit(1);
            }
        }
        close(pipefd[0]);
        _exit(0);
    }

    close(pipefd[0]); /* parent only writes */
    if (argc == 1) {
        if (write_all(pipefd[1], "Hello, Unix PiPe! aBc 123\n", sizeof("Hello, Unix PiPe! aBc 123\n") - 1) == -1)
            perror("parent write");
    } else {
        for (i = 1; i < argc; ++i) {
            const char *s = argv[i];
            size_t len = 0;
            while (s[len])
                ++len;
            if (write_all(pipefd[1], s, len) == -1 ||
                write_all(pipefd[1], i == argc - 1 ? "\n" : " ", 1) == -1) {
                perror("parent write");
                break;
            }
        }
    }
    close(pipefd[1]); /* EOF for child */

    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 1;
    return 0;
}
