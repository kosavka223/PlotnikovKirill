#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void)
{
    time_t now;
    struct tm california;
    char buf[128];

    if (setenv("TZ", "PST8", 1) == -1) {
        perror("setenv");
        return 1;
    }
    tzset();

    if (time(&now) == (time_t)-1) {
        perror("time");
        return 1;
    }

    if (localtime_r(&now, &california) == NULL) {
        perror("localtime_r");
        return 1;
    }

    if (strftime(buf, sizeof(buf), "%A, %d %B %Y, %H:%M:%S PST", &california) == 0) {
        fprintf(stderr, "strftime: buffer is too small\n");
        return 1;
    }

    printf("%s\n", buf);
    return 0;
}
