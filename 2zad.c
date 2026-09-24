#include <stdio.h>
#include <time.h>

int main(void)
{
    time_t t;
    struct tm *tmp;
    char buf[BUFSIZ];

    time(&t);            /* текущее время: секунды с 01.01.1970 UTC */
    t -= 8 * 60 * 60;    /* сдвиг на 8 часов назад: PST = UTC - 8   */
    tmp = gmtime(&t);    /* разбираем как UTC — получаем PST        */

    strftime(buf, sizeof(buf), "%A, %d %B %Y, %H:%M:%S (PST)", tmp);
    printf("%s\n", buf);

    return 0;
}