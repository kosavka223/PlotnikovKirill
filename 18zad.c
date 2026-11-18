}

static void permissions(mode_t m, char p[11])
{
    p[0] = S_ISDIR(m) ? 'd' : (S_ISREG(m) ? '-' : '?');
    p[1] = (m & S_IRUSR) ? 'r' : '-';
    p[2] = (m & S_IWUSR) ? 'w' : '-';
    p[3] = (m & S_IXUSR) ? 'x' : '-';
    p[4] = (m & S_IRGRP) ? 'r' : '-';
    p[5] = (m & S_IWGRP) ? 'w' : '-';
    p[6] = (m & S_IXGRP) ? 'x' : '-';
    p[7] = (m & S_IROTH) ? 'r' : '-';
    p[8] = (m & S_IWOTH) ? 'w' : '-';
    p[9] = (m & S_IXOTH) ? 'x' : '-';
    p[10] = '\0';
}

static int show_file(const char *path)
{
    struct stat st;
    struct passwd *pw;
    struct group *gr;
    struct tm tm_value;
    char mode[11], date[32], owner[64], group[64], size[32];

    /* ls -ld: show the path itself rather than listing its directory contents. */
    if (lstat(path, &st) == -1) {
        perror(path);
        return 1;
    }

    permissions(st.st_mode, mode);
    pw = getpwuid(st.st_uid);
    if (pw)
        snprintf(owner, sizeof owner, "%s", pw->pw_name);
    else
        snprintf(owner, sizeof owner, "%lu", (unsigned long)st.st_uid);

    gr = getgrgid(st.st_gid);
    if (gr)
        snprintf(group, sizeof group, "%s", gr->gr_name);
    else
        snprintf(group, sizeof group, "%lu", (unsigned long)st.st_gid);

    if (S_ISREG(st.st_mode))
        snprintf(size, sizeof size, "%lld", (long long)st.st_size);
    else
        size[0] = '\0';

    if (localtime_r(&st.st_mtime, &tm_value) != NULL)
        strftime(date, sizeof date, "%Y-%m-%d %H:%M", &tm_value);
    else
        snprintf(date, sizeof date, "unknown");

    printf("%-10s %4lu %-14s %-14s %12s %16s %s\n",
           mode, (unsigned long)st.st_nlink, owner, group,
           size, date, base_name(path));
    return 0;
}

int main(int argc, char **argv)
{
    int i, failed = 0;

    if (argc == 1)
        return show_file(".");
    for (i = 1; i < argc; ++i)
        failed |= show_file(argv[i]);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
