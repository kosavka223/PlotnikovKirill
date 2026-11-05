
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Использование: %s команда [аргументы...]\n", argv[0]);
        return 1;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return 1;
    } else if (pid == 0) {
        execvp(argv[1], &argv[1]);
        perror("execvp");
        _exit(127);
    } else {
        int status;

        if (waitpid(pid, &status, 0) == -1) {
            perror("waitpid");
            return 1;
        }

        if (WIFEXITED(status)) {
            printf("Код завершения: %d\n", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            printf("Процесс завершён сигналом: %d\n", WTERMSIG(status));
        }
    }

    return 0;
}
