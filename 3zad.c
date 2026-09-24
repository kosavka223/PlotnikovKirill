#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

int main(int argc, char *argv[])
{
    FILE *fp;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Шаг 1: печатаем реальный и эффективный UID */
    printf("uid=%d euid=%d\n", (int)getuid(), (int)geteuid());

    /* Шаг 2: открываем файл */
    if ((fp = fopen(argv[1], "r")) == NULL)
        perror(argv[1]);
    else {
        printf("fopen: OK\n");
        fclose(fp);
    }

    /* Шаг 3: делаем эффективный UID равным реальному */
    setuid(getuid());

    /* Шаг 4: повторяем шаги 1 и 2 */
    printf("posle setuid: uid=%d euid=%d\n", (int)getuid(), (int)geteuid());
    if ((fp = fopen(argv[1], "r")) == NULL)
        perror(argv[1]);
    else {
        printf("fopen: OK\n");
        fclose(fp);
    }

    return 0;
}