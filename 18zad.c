
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <unistd.h>

/* Получаем имя файла без пути */
static const char *base_name(const char *path)
{
    const char *end = path + strlen(path);
    const char *begin;
    static char name[4096];
    size_t n;

    while (end > path + 1 && end[-1] == '/')
        --end;

    begin = end;

    while (begin > path && begin[-1] != '/')
        --begin;

    if (begin == end && end == path + 1 && *path == '/')
        return "/";

    n = (size_t)(end - begin);

    if (n >= sizeof(name))
        n = sizeof(name) - 1;

    memcpy(name, begin, n);
    name[n] = '\0';

    return name;
}

/* Определяем тип файла и права доступа */
static void permissions(mode_t m, char p[11])
{
    p[0] = S_ISDIR(m) ? 'd' : (S_ISREG(m) ? '-' : '?');

    /* Права владельца */
    p[1] = (m & S_IRUSR) ? 'r' : '-';
    p[2] = (m & S_IWUSR) ? 'w' : '-';
    p[3] = (m & S_IXUSR) ? 'x' : '-';

    /* Права группы */
    p[4] = (m & S_IRGRP) ? 'r' : '-';
    p[5] = (m & S_IWGRP) ? 'w' : '-';
    p[6] = (m & S_IXGRP) ? 'x' : '-';

    /* Права остальных пользователей */
    p[7] = (m & S_IROTH) ? 'r' : '-';
    p[8] = (m & S_IWOTH) ? 'w' : '-';
    p[9] = (m & S_IXOTH) ? 'x' : '-';

    p[10] = '\0';
}

/* Вывод информации об одном файле */
static int show_file(const char *path)
{
    struct stat st;
    struct passwd *pw;
    struct group *gr;
    struct tm tm_value;

    char mode[11];
    char date[32];
    char owner[64];
    char group[64];
    char size[32];

    /* Получаем сведения о файле */
    if (lstat(path, &st) == -1) {
        perror(path);
        return 1;
    }

    /* Тип и права доступа */
    permissions(st.st_mode, mode);

    /* Имя владельца */
    pw = getpwuid(st.st_uid);

    if (pw)
        snprintf(owner, sizeof(owner), "%s", pw->pw_name);
    else
        snprintf(owner, sizeof(owner), "%lu",
                 (unsigned long)st.st_uid);

    /* Имя группы */
    gr = getgrgid(st.st_gid);

    if (gr)
        snprintf(group, sizeof(group), "%s", gr->gr_name);
    else
        snprintf(group, sizeof(group), "%lu",
                 (unsigned long)st.st_gid);

    /* Размер только для обычных файлов */
    if (S_ISREG(st.st_mode))
        snprintf(size, sizeof(size), "%lld",
                 (long long)st.st_size);
    else
        size[0] = '\0';

    /* Дата последней модификации */
    if (localtime_r(&st.st_mtime, &tm_value) != NULL)
        strftime(date, sizeof(date),
                 "%Y-%m-%d %H:%M", &tm_value);
    else
        snprintf(date, sizeof(date), "unknown");

    /* Вывод таблицы */
    printf("%-10s %4lu %-14s %-14s %12s %16s %s\n",
           mode,
           (unsigned long)st.st_nlink,
           owner,
           group,
           size,
           date,
           base_name(path));

    return 0;
}

/* Главная функция */
int main(int argc, char *argv[])
{
    int i;
    int failed = 0;

    /* Если аргументов нет, показываем текущий каталог */
    if (argc == 1)
        return show_file(".");

    /* Обрабатываем каждый переданный файл */
    for (i = 1; i < argc; ++i)
        failed |= show_file(argv[i]);

    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
