
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#define WIDTH 40
#define MAX_INPUT 4096
#define CTRL_D 4
#define CTRL_G 7
#define CTRL_W 23

static struct termios old_termios;
static int terminal_changed = 0;

static char line[MAX_INPUT + 1];
static size_t length = 0;
static size_t shown_rows = 1;

static void restore_terminal(void)
{
    if (terminal_changed)
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
}

static void beep(void)
{
    putchar(CTRL_G);
    fflush(stdout);
}

static void next_row(size_t *column, size_t *rows)
{
    fputs("\r\n", stdout);
    *column = 0;
    ++*rows;
}

static void redraw(void)
{
    size_t i = 0, j, column = 0, rows = 1;

    putchar('\r');

    if (shown_rows > 1)
        printf("\033[%zuA", shown_rows - 1);

    fputs("\033[J", stdout);

    while (i < length) {
        if (line[i] == ' ') {
            if (column == WIDTH)
                next_row(&column, &rows);

            putchar(' ');
            ++column;
            ++i;
            continue;
        }

        j = i;

        while (j < length && line[j] != ' ')
            ++j;

        if (column > 0 && column + (j - i) > WIDTH)
            next_row(&column, &rows);

        while (i < j) {
            if (column == WIDTH)
                next_row(&column, &rows);

            putchar(line[i]);
            ++column;
            ++i;
        }
    }

    shown_rows = rows;
    fflush(stdout);
}

static void erase_word(void)
{
    while (length > 0 && line[length - 1] == ' ')
        --length;

    while (length > 0 && line[length - 1] != ' ')
        --length;

    line[length] = '\0';
}

int main(void)
{
    struct termios raw;
    unsigned char ch;
    ssize_t count;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        fprintf(stderr, "Run in a terminal.\n");
        return 1;
    }

    if (tcgetattr(STDIN_FILENO, &old_termios) == -1) {
        perror("tcgetattr");
        return 1;
    }

    raw = old_termios;

    raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("tcsetattr");
        return 1;
    }

    terminal_changed = 1;
    atexit(restore_terminal);

    printf("Line editor (40 columns)\r\n");
    fflush(stdout);

    while (1) {
        count = read(STDIN_FILENO, &ch, 1);

        if (count == -1) {
            if (errno == EINTR)
                continue;
            perror("read");
            break;
        }

        if (count == 0)
            break;

        if (ch == old_termios.c_cc[VERASE]) {
            if (length > 0) {
                line[--length] = '\0';
                redraw();
            } else {
                beep();
            }
        }
        else if (ch == old_termios.c_cc[VKILL]) {
            length = 0;
            line[0] = '\0';
            redraw();
        }
        else if (ch == CTRL_W) {
            if (length > 0) {
                erase_word();
                redraw();
            } else {
                beep();
            }
        }
        else if (ch == CTRL_D) {
            if (length == 0) {
                printf("\r\n");
                break;
            }
            beep();
        }
        else if (ch == '\n' || ch == '\r') {
            printf("\r\n");
            length = 0;
            line[0] = '\0';
            shown_rows = 1;
        }
        else if (ch >= 32 && ch <= 126) {
            if (length >= MAX_INPUT) {
                beep();
            } else {
                line[length++] = ch;
                line[length] = '\0';
                redraw();
            }
        }
        else {
            beep();
        }
    }

    fflush(stdout);
    return 0;
}
