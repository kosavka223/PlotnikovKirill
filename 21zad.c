#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

static volatile sig_atomic_t count = 0;
static volatile sig_atomic_t finish = 0;

static void on_int(int signum)
{
    const char bell = '\a';
    (void)signum;
    ++count;
    /* write() is async-signal-safe; printf() is not. */
    (void)write(STDOUT_FILENO, &bell, 1);
}

static void on_quit(int signum)
{
    (void)signum;
    finish = 1;
}

int main(void)
{
    struct sigaction sa;
    sigset_t block, previous;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = on_int;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
        return EXIT_FAILURE;
    }
    sa.sa_handler = on_quit;
    if (sigaction(SIGQUIT, &sa, NULL) == -1) {
        perror("sigaction SIGQUIT");
        return EXIT_FAILURE;
    }

    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGQUIT);
    if (sigprocmask(SIG_BLOCK, &block, &previous) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    puts("Ctrl+C: beep (BEL). Ctrl+\\: finish and print the count.");
    fflush(stdout);

    while (!finish)
        sigsuspend(&previous);

    if (sigprocmask(SIG_SETMASK, &previous, NULL) == -1) {
        perror("sigprocmask restore");
        return EXIT_FAILURE;
    }
    printf("\nNumber of beeps: %d\n", (int)count);
    return EXIT_SUCCESS;
}
