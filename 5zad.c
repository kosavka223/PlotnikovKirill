#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>

struct line_info {
    off_t offset;
    size_t length;
};

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

    if (line_length > 0) {
        if (add_line(table, count, &capacity, line_start, line_length) == -1)
            return -1;
    }

    return 0;
}

static int print_line(int fd, const struct line_info *line)
{
    char *buf;
    size_t done = 0;

    if (lseek(fd, line->offset, SEEK_SET) == (off_t)-1)
        return -1;

    buf = malloc(line->length + 1);
    if (buf == NULL)
        return -1;

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

    buf[done] = '\0';
    printf("%s", buf);
    if (done > 0 && buf[done - 1] != '\n')
        putchar('\n');

    free(buf);
    return 0;
}

static int parse_number(const char *s, long *value)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno == ERANGE || end == s)
        return -1;

    while (*end == ' ' || *end == '\t')
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

    for (;;) {
        long number;

        printf("Line number (0 to exit, 1..%zu): ", line_count);
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

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
    if (close(fd) == -1)
        perror("close");
    return 0;
}
