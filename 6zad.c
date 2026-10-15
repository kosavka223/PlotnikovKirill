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
    char *buf;
    size_t done = 0;
    if (line->length == 0)
        return 0;
    buf = malloc(line->length);
    if (buf == NULL)
        return -1;
    if (lseek(fd, line->offset, SEEK_SET) == (off_t)-1) {
