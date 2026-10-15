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
    int first_input_received = 0;

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

        if (!first_input_received)
            printf("Line number within 5 seconds (0 to exit, 1..%zu): ", line_count);
        else
            printf("Line number (no time limit, 0 to exit, 1..%zu): ", line_count);
        fflush(stdout);

        /* Time limit applies only until the first complete input arrives. */
        timed_out = 0;
        if (!first_input_received)
            alarm(5);
        n = read(STDIN_FILENO, input, sizeof(input) - 1);
        if (!first_input_received)
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

        /* Any received first line (even invalid) switches the timer off forever. */
        first_input_received = 1;
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
