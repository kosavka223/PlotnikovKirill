#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

static void print_ids(const char *stage)
{
    printf("%s: uid=%ld euid=%ld\n",
           stage, (long)getuid(), (long)geteuid());
}

static void try_open(const char *filename)
{
    FILE *fp = fopen(filename, "r");

    if (fp == NULL) {
        perror("fopen");
        return;
    }

    printf("fopen: success\n");
    if (fclose(fp) == EOF)
        perror("fclose");
}

int main(int argc, char *argv[])
{
    uid_t real_uid;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    print_ids("before setuid");
    try_open(argv[1]);

    real_uid = getuid();
    if (setuid(real_uid) == -1) {
        perror("setuid");
        return EXIT_FAILURE;
    }

    print_ids("after setuid");
    try_open(argv[1]);

    return 0;
}
