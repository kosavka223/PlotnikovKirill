#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>

struct line_info {
    size_t offset;
    size_t length;
};

static volatile sig_atomic_t timed_out = 0;

static void alarm_handler(int signo)
{
    (void)signo;
    timed_out = 1;
}

static int add_line(struct line_info **table, size_t *count, size_t *capacity,
                    size_t offset, size_t length)
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

static int build_table(const char *data, size_t size,
                       struct line_info **table, size_t *count)
{
    size_t capacity = 0;
    size_t start = 0;

    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\n') {
            if (add_line(table, count, &capacity, start, i - start + 1) == -1)
                return -1;
            start = i + 1;
        }
    }

    if (start < size) {
        if (add_line(table, count, &capacity, start, size - start) == -1)
            return -1;
    }
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
    struct stat st;
    char *data = NULL;
    size_t size;
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

    if (fstat(fd, &st) == -1) {
        perror("fstat");
        close(fd);
        return EXIT_FAILURE;
    }
    if (st.st_size < 0) {
        fprintf(stderr, "Invalid file size\n");
        close(fd);
        return EXIT_FAILURE;
    }
    size = (size_t)st.st_size;

    if (size > 0) {
        data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            perror("mmap");
            close(fd);
            return EXIT_FAILURE;
        }
    }

    if (build_table(data, size, &table, &line_count) == -1) {
        perror("realloc");
        free(table);
        if (size > 0)
            munmap(data, size);
        close(fd);
        return EXIT_FAILURE;
    }

    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        free(table);
        if (size > 0)
            munmap(data, size);
        close(fd);
        return EXIT_FAILURE;
    }

    for (;;) {
        long number;

        printf("Line number within 5 seconds (0 to exit, 1..%zu): ", line_count);
        fflush(stdout);

        timed_out = 0;
        alarm(5);
        errno = 0;
        if (fgets(input, sizeof(input), stdin) == NULL) {
            alarm(0);
            if (timed_out || errno == EINTR) {
                clearerr(stdin);
                printf("\nTime is over. Whole file:\n");
                if (size > 0)
                    fwrite(data, 1, size, stdout);
                if (size > 0 && data[size - 1] != '\n')
                    putchar('\n');
            }
            break;
        }
        alarm(0);

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

        {
            struct line_info line = table[number - 1];
            fwrite(data + line.offset, 1, line.length, stdout);
            if (line.length > 0 && data[line.offset + line.length - 1] != '\n')
                putchar('\n');
        }
    }

    free(table);
    if (size > 0 && munmap(data, size) == -1)
        perror("munmap");
    close(fd);
    return 0;
}
