#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <ulimit.h>
#include <sys/resource.h>

extern char **environ;

struct option_record {
    int option;
    char *arg;
};

static int parse_nonnegative_long(const char *s, long *value)
{
    char *end;
    long v;

    if (s == NULL || *s == '\0')
        return -1;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno == ERANGE || end == s || *end != '\0' || v < 0)
        return -1;

    *value = v;
    return 0;
}

static void print_ids(void)
{
    printf("uid=%ld euid=%ld gid=%ld egid=%ld\n",
           (long)getuid(), (long)geteuid(),
           (long)getgid(), (long)getegid());
}

static void make_group_leader(void)
{
    if (setpgid(0, 0) == -1)
        perror("setpgid");
}

static void print_process_ids(void)
{
    printf("pid=%ld ppid=%ld pgid=%ld\n",
           (long)getpid(), (long)getppid(), (long)getpgrp());
}

static void print_ulimit(void)
{
    errno = 0;
    long value = ulimit(UL_GETFSIZE);
    if (value == -1 && errno != 0)
        perror("ulimit");
    else
        printf("ulimit=%ld (blocks of 512 bytes)\n", value);
}

static void set_ulimit_value(const char *arg)
{
    long value;

    if (parse_nonnegative_long(arg, &value) == -1) {
        fprintf(stderr, "Bad value for -U: %s\n", arg ? arg : "(null)");
        return;
    }

    errno = 0;
    if (ulimit(UL_SETFSIZE, value) == -1 && errno != 0)
        perror("ulimit(UL_SETFSIZE)");
}

static void print_core_limit(void)
{
    struct rlimit lim;

    if (getrlimit(RLIMIT_CORE, &lim) == -1) {
        perror("getrlimit");
        return;
    }

    if (lim.rlim_cur == RLIM_INFINITY)
        printf("core=unlimited\n");
    else
        printf("core=%llu bytes\n", (unsigned long long)lim.rlim_cur);
}

static void set_core_limit(const char *arg)
{
    struct rlimit lim;
    long value;

    if (parse_nonnegative_long(arg, &value) == -1) {
        fprintf(stderr, "Bad value for -C: %s\n", arg ? arg : "(null)");
        return;
    }

    if (getrlimit(RLIMIT_CORE, &lim) == -1) {
        perror("getrlimit");
        return;
    }

    if ((rlim_t)value > lim.rlim_max) {
        fprintf(stderr, "-C: value exceeds hard limit\n");
        return;
    }

    lim.rlim_cur = (rlim_t)value;
    if (setrlimit(RLIMIT_CORE, &lim) == -1)
        perror("setrlimit");
}

static void print_cwd(void)
{
    char *cwd = getcwd(NULL, 0);

    if (cwd == NULL) {
        perror("getcwd");
        return;
    }

    printf("cwd=%s\n", cwd);
    free(cwd);
}

static void print_environment(void)
{
    char **p;
    for (p = environ; *p != NULL; ++p)
        puts(*p);
}

static void set_environment(const char *arg)
{
    char *copy;

    if (arg == NULL || strchr(arg, '=') == NULL || arg[0] == '=') {
        fprintf(stderr, "Bad value for -V, expected name=value: %s\n",
                arg ? arg : "(null)");
        return;
    }

    copy = strdup(arg);
    if (copy == NULL) {
        perror("strdup");
        return;
    }

    if (putenv(copy) != 0) {
        perror("putenv");
        free(copy);
    }
    /* On success putenv() keeps using copy, so we must not free it here. */
}

int main(int argc, char *argv[])
{
    struct option_record *records;
    int count = 0;
    int opt;
    int i;

    records = calloc((size_t)argc, sizeof(*records));
    if (records == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    opterr = 0;
    while ((opt = getopt(argc, argv, ":ispuU:cC:dvV:")) != -1) {
        if (opt == '?') {
            fprintf(stderr, "Unknown option: -%c\n", optopt);
            continue;
        }
        if (opt == ':') {
            fprintf(stderr, "Option -%c requires an argument\n", optopt);
            continue;
        }

        records[count].option = opt;
        records[count].arg = optarg;
        ++count;
    }

    for (i = count - 1; i >= 0; --i) {
        switch (records[i].option) {
        case 'i': print_ids(); break;
        case 's': make_group_leader(); break;
        case 'p': print_process_ids(); break;
        case 'u': print_ulimit(); break;
        case 'U': set_ulimit_value(records[i].arg); break;
        case 'c': print_core_limit(); break;
        case 'C': set_core_limit(records[i].arg); break;
        case 'd': print_cwd(); break;
        case 'v': print_environment(); break;
        case 'V': set_environment(records[i].arg); break;
        }
    }

    while (optind < argc) {
        fprintf(stderr, "Non-option argument: %s\n", argv[optind]);
        ++optind;
    }

    free(records);
    return 0;
}
