#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ulimit.h>
#include <sys/resource.h>

extern char **environ;

int main(int argc, char *argv[])
{
    int c;
    char cwd[BUFSIZ];
    struct rlimit rl;
    char *end;
    long v;

    while ((c = getopt(argc, argv, "ispu:U:cC:dvV:")) != -1) {
        switch (c) {

        case 'i':   /* реальные и эффективные uid/gid */
            printf("uid=%d euid=%d gid=%d egid=%d\n",
                (int)getuid(), (int)geteuid(), (int)getgid(), (int)getegid());
            break;

        case 's':   /* стать лидером группы */
            if (setpgid(0, 0) != 0)
                perror("setpgid");
            break;

        case 'p':   /* pid, ppid, группа процессов */
            printf("pid=%d ppid=%d pgrp=%d\n",
                (int)getpid(), (int)getppid(), (int)getpgrp());
            break;

        case 'u':   /* ulimit */
            v = ulimit(UL_GETFSIZE);
            if (v == -1)
                perror("ulimit");
            else
                printf("ulimit(fsize) = %ld blocks (512 bytes)\n", v);
            break;

        case 'U':   /* изменить ulimit */
            v = strtol(optarg, &end, 10);
            if (optarg[0] == '\0' || *end != '\0' || v <= 0) {
                fprintf(stderr, "U: bad value: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            if (ulimit(UL_SETFSIZE, v) == -1) {
                perror("ulimit");
                exit(EXIT_FAILURE);
            }
            break;

        case 'c':   /* размер core-файла */
            if (getrlimit(RLIMIT_CORE, &rl) != 0) {
                perror("getrlimit");
            } else if (rl.rlim_cur == RLIM_INFINITY) {
                printf("core size: unlimited\n");
            } else {
                printf("core size: %llu bytes\n", (unsigned long long)rl.rlim_cur);
            }
            break;

        case 'C':   /* изменить размер core-файла */
            if (getrlimit(RLIMIT_CORE, &rl) != 0) {
                perror("getrlimit");
            } else {
                rl.rlim_cur = (rlim_t)strtoul(optarg, &end, 10);
                if (optarg[0] == '\0' || *end != '\0') {
                    fprintf(stderr, "C: bad value: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                if (setrlimit(RLIMIT_CORE, &rl) != 0)
                    perror("setrlimit");
            }
            break;

        case 'd':   /* текущий каталог */
            if (getcwd(cwd, sizeof(cwd)) == NULL)
                perror("getcwd");
            else
                printf("cwd: %s\n", cwd);
            break;

        case 'v':   /* переменные среды */
            for (char **ep = environ; *ep != NULL; ep++)
                puts(*ep);
            break;

        case 'V':   /* новая переменная среды name=value */
            if (strchr(optarg, '=') == NULL) {
                fprintf(stderr, "V: expected name=value, got: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            if (putenv(optarg) != 0)
                perror("putenv");
            break;

        case '?':   /* недопустимая опция — getopt уже напечатал ошибку */
        default:
            exit(EXIT_FAILURE);
        }
    }
    return 0;
}