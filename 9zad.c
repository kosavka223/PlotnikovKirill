
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    printf("Родитель: запускаю cat\n");
    fflush(stdout);

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return 1;
    } else if (pid == 0) {
        execlp("cat", "cat", "longfile.txt", (char *)NULL);
        perror("execlp");
        _exit(1);
    } else {
        printf("Родитель: дочерний процесс работает\n");
        fflush(stdout);

        int status;
        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            return 1;
        }

        printf("Родитель: дочерний процесс завершился\n");
    }

    return 0;
}
