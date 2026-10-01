/*
 * Задание 1. Вывод различных атрибутов процесса в соответствии
 * с указанными опциями. Опции применяются в порядке появления
 * справа налево (по условию задачи).
 *
 * man -s 3C getopt, man -s 2 setpgid, man -s 2 getrlimit, man -s 3 ulimit
 * Сборка: gcc -Wall -o 1zad 1zad.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ulimit.h>
#include <sys/resource.h>

extern char *optarg;        /* аргумент текущей опции, заполняется getopt */
extern int   optind, opterr, optopt;
extern char **environ;      /* среда процесса, см. environ(5) */

static const char *opt_list = "ispuU:cC:dvV:";

/* одна разобранная опция: буква + её аргумент (или NULL) */
struct optrec {
    char letter;
    char *value;
};

/* -i: реальные и эффективные идентификаторы (аналог id(1)) */
static void show_ids(void)
{
    printf("UID - %d\teUID - %d\tGID - %d\teGID - %d\n",
           (int)getuid(), (int)geteuid(), (int)getgid(), (int)getegid());
}

/* -s: процесс становится лидером группы процессов */
static void become_leader(void)
{
    if (setpgid(0, 0) == -1)
        perror("setpgid");
}

/* -p: номер процесса, родителя и группы процессов */
static void show_pids(void)
{
    printf("PID - %d\tPGID - %d\tPPID - %d\n",
           (int)getpid(), (int)getpgrp(), (int)getppid());
}

/* -u: потолок размера файла (в блоках по 512 байт, как ulimit(1)) */
static void show_file_limit(void)
{
    long cur = ulimit(UL_GETFSIZE);

    if (cur < 0)                    /* unlimited приходит как -1 */
        printf("ULIMIT - unlimited\n");
    else
        printf("ULIMIT - %ld\n", cur);
}

/* -U: назначить новый потолок размера файла */
static void apply_file_limit(const char *text)
{
    char *tail;
    long value = strtol(text, &tail, 10);

    if (tail == text || *tail != '\0' || value < 0) {
        printf("Некорректное значение для -U: %s\n", text);
        return;
    }
    if (ulimit(UL_SETFSIZE, value) == -1)
        perror("ulimit(UL_SETFSIZE)");
}

/* печать лимита с учётом special-значения unlimited */
static void print_core_value(unsigned long v)
{
    if (v == (unsigned long)RLIM_INFINITY)
        printf("CORE - unlimited\n");
    else
        printf("CORE - %lu\n", v);
}

/* -c: размер в байтах core-файла, который может быть создан */
static void show_core_limit(void)
{
    struct rlimit core;

    if (getrlimit(RLIMIT_CORE, &core) == -1) {
        perror("getrlimit");
        return;
    }
    print_core_value((unsigned long)core.rlim_cur);
}

/* -C: изменить максимально возможный размер core-файла */
static void apply_core_limit(const char *text)
{
    struct rlimit core;
    char *tail;
    long value = strtol(text, &tail, 10);

    if (tail == text || *tail != '\0' || value < 0) {
        printf("Некорректное значение для -C: %s\n", text);
        return;
    }
    if (getrlimit(RLIMIT_CORE, &core) == -1) {  /* сохраняем текущий hard-лимит */
        perror("getrlimit");
        return;
    }
    core.rlim_cur = (rlim_t)value;
    if (setrlimit(RLIMIT_CORE, &core) == -1)
        printf("setrlimit: нельзя назначить %ld\n", value);
}

/* -d: текущий рабочий каталог (аналог pwd(1)) */
static void show_workdir(void)
{
    char *here = getcwd(NULL, 0);

    if (here == NULL) {
        perror("getcwd");
        return;
    }
    printf("DIR - %s\n", here);
    free(here);                     /* getcwd(NULL,0) даёт malloc-нутый буфер */
}

/* -v: все переменные среды с их значениями (аналог env(1)) */
static void show_environ(void)
{
    char **entry;

    for (entry = environ; *entry != NULL; entry++)
        printf("%s\n", *entry);
}

int main(int argc, char *argv[])
{
    struct optrec seen[argc];       /* опции в порядке появления */
    int count = 0;
    int ch, k;

    /* 1. разбираем все опции через getopt(3C) */
    while ((ch = getopt(argc, argv, opt_list)) != -1) {
        if (ch == '?') {
            printf("Неизвестный аргумент - -%c\n", optopt);
            continue;
        }
        seen[count].letter = (char)ch;
        seen[count].value  = optarg;    /* NULL у опций без аргумента */
        count++;
    }

    /* 2. применяем справа налево — как требует условие */
    for (k = count - 1; k >= 0; k--) {
        switch (seen[k].letter) {
        case 'i': show_ids();                      break;
        case 's': become_leader();                 break;
        case 'p': show_pids();                     break;
        case 'u': show_file_limit();               break;
        case 'U': apply_file_limit(seen[k].value); break;
        case 'c': show_core_limit();               break;
        case 'C': apply_core_limit(seen[k].value); break;
        case 'd': show_workdir();                  break;
        case 'v': show_environ();                  break;
        case 'V': putenv(seen[k].value);           break;
        }
    }

    /* 3. всё, что не является опцией */
    while (optind < argc)
        printf("Неподдерживаевый аргумент - %s\n", argv[optind++]);

    return 0;
}
