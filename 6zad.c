#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>

struct line_info {
    off_t offset;
    size_t length;
};

static volatile sig_atomic_t timed_out = 0;

static void alarm_handler(int signo)
{
    (void)signo;
    timed_out = 1;
}

static int add_line(struct line_info **table, size_t *count, size_t *capacity,
                    off_t offset, size_t length)
{
    if (*count == *capacity) {
        size_t new_capacity = (*capacity == 0) ? 16 : *capacity * 2;
        struct line_info *tmp = realloc(*table, new_capacity * sizeof(**table));
        if (tmp == NULL)
            return -1;
        *table = tmp;
        *capacity = new_capacity;
    }
    (*table)[*count].offset = offset;
    (*table)[*count].length = length;
    ++(*count);
    return 0;
}

static int build_table(int fd, struct line_info **table, size_t *count)
{
    char ch;
    ssize_t n;
    off_t line_start = 0;
    size_t line_length = 0;
    size_t capacity = 0;

    while ((n = read(fd, &ch, 1)) > 0) {
        ++line_length;
        if (ch == '\n') {
            if (add_line(table, count, &capacity, line_start, line_length) == -1)
                return -1;
            line_start = lseek(fd, 0, SEEK_CUR);
            if (line_start == (off_t)-1)
                return -1;
            line_length = 0;
        }
    }
    if (n == -1)
        return -1;
    if (line_length > 0 && add_line(table, count, &capacity, line_start, line_length) == -1)
        return -1;
    return 0;
}

static int copy_all(int fd)
{
    char buf[4096];
    ssize_t n;

    if (lseek(fd, 0, SEEK_SET) == (off_t)-1)
        return -1;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t done = 0;
        while (done < n) {
            ssize_t w = write(STDOUT_FILENO, buf + done, (size_t)(n - done));
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                return -1;
            }
            done += w;
        }
    }
    return (n < 0) ? -1 : 0;
}

static int print_line(int fd, const struct line_info *line)
{
    char *buf = malloc(line->length);
    size_t done = 0;

    if (line->length == 0)
        return 0;
    if (buf == NULL)
        return -1;
    if (lseek(fd, line->offset, SEEK_SET) == (off_t)-1) {
        free(buf);
        return -1;
    }

    while (done < line->length) {
        ssize_t n = read(fd, buf + done, line->length - done);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            free(buf);
            return -1;
        }
        if (n == 0)
            break;
        done += (size_t)n;
    }

    if (write(STDOUT_FILENO, buf, done) < 0) {
        free(buf);
        return -1;
    }
    if (done > 0 && buf[done - 1] != '\n')
        (void)write(STDOUT_FILENO, "\n", 1);

    free(buf);
    return 0;
}

static int parse_number(char *s, long *value)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno == ERANGE || end == s)
        return -1;
    while (*end == ' ' || *end == '\t' || *end == '\r')
        ++end;
    if (*end != '\n' && *end != '\0')
        return -1;
    *value = v;
    return 0;
}

int main(int argc, char *argv[])
{
    int fd;
    struct line_info *table = NULL;
    size_t line_count = 0;
    struct sigaction sa;
    char input[128];

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <text-file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        perror("open");
        return EXIT_FAILURE;
    }
    if (build_table(fd, &table, &line_count) == -1) {
        perror("build table");
        free(table);
        close(fd);
        return EXIT_FAILURE;
    }

    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* do not restart interrupted input */
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        free(table);
        close(fd);
        return EXIT_FAILURE;
    }

    for (;;) {
        ssize_t n;
        long number;

        printf("Line number within 5 seconds (0 to exit, 1..%zu): ", line_count);
        fflush(stdout);

        timed_out = 0;
        alarm(5);
        n = read(STDIN_FILENO, input, sizeof(input) - 1);
        alarm(0);

        if (n < 0) {
            if (errno == EINTR && timed_out) {
                printf("\nTime is over. Whole file:\n");
                fflush(stdout);
                if (copy_all(fd) == -1)
                    perror("copy file");
                break;
            }
            perror("read stdin");
            break;
        }
        if (n == 0)
            break;

        input[n] = '\0';
        if (parse_number(input, &number) == -1) {
            printf("Invalid number\n");
            continue;
        }
        if (number == 0)
            break;
        if (number < 1 || (size_t)number > line_count) {
            printf("No such line\n");
            continue;
        }
        if (print_line(fd, &table[number - 1]) == -1) {
            perror("print line");
            break;
        }
    }

    free(table);
    close(fd);
    return 0;
}
