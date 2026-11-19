
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <string.h>
#include <signal.h>

#define WIDTH 40
#define MAX_TEXT 8192

static struct termios old_term;
static int terminal_changed = 0;

static char text[MAX_TEXT];
static size_t len = 0;
static size_t line_start = 0;
static size_t shown = 0;

/* Восстановление настроек терминала */
static void restore_terminal(void)
{
    if (terminal_changed)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old_term);
}

/* Вывод символа CTRL-G */
static void beep(void)
{
    putchar('\a');
    fflush(stdout);
}

/* Удаление одного символа с экрана */
static void erase_screen(void)
{
    fputs("\b \b", stdout);
}

/* Удаление видимой текущей строки */
static void clear_screen_line(void)
{
    while (shown > 0) {
        erase_screen();
        shown--;
    }
    fflush(stdout);
}

/* Отображение текущей строки с переносом слов */
static void redraw(void)
{
    size_t i = line_start;
    size_t col = 0;

    clear_screen_line();

    while (i < len) {
        size_t j = i;

        if (text[i] == ' ') {
            if (col < WIDTH) {
                putchar(' ');
                col++;
            }
            i++;
            continue;
        }

        while (j < len && text[j] != ' ')
            j++;

        size_t word_len = j - i;

        if (col > 0 && col + word_len > WIDTH) {
            putchar('\n');
            col = 0;
        }

        while (i < j) {
            putchar(text[i++]);
            col++;

            if (col == WIDTH && i < len) {
                putchar('\n');
                col = 0;
            }
        }
    }

    shown = col;
    fflush(stdout);
}

/* Удаление последнего символа */
static void erase_last(void)
{
    if (len > line_start) {
        len--;
        redraw();
    } else {
        beep();
    }
}

/* Удаление текущей строки */
static void kill_line(void)
{
    len = line_start;
    redraw();
}

/* Удаление последнего слова и пробелов после него */
static void erase_word(void)
{
    if (len == line_start) {
        beep();
        return;
    }

    while (len > line_start && text[len - 1] == ' ')
        len--;

    while (len > line_start && text[len - 1] != ' ')
        len--;

    redraw();
}

/* Пропуск последовательности стрелки */
static void skip_escape(void)
{
    unsigned char c;

    if (read(STDIN_FILENO, &c, 1) != 1)
        return;

    if (c == '[' || c == 'O') {
        do {
            if (read(STDIN_FILENO, &c, 1) != 1)
                return;
        } while (c < 0x40 || c > 0x7e);
    }

    beep();
}

int main(void)
{
    struct termios new_term;
    unsigned char c;
    unsigned char erase_char;
    unsigned char kill_char;

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Input must be a terminal\n");
        return EXIT_FAILURE;
    }

    if (tcgetattr(STDIN_FILENO, &old_term) == -1) {
        perror("tcgetattr");
        return EXIT_FAILURE;
    }

    erase_char = old_term.c_cc[VERASE];
    kill_char = old_term.c_cc[VKILL];

    new_term = old_term;

    /* Выключаем эхо и каноническую обработку */
    new_term.c_lflag &= ~(ECHO | ICANON);

    /* Отключаем обработку управляющих символов */
    new_term.c_lflag &= ~ISIG;

    new_term.c_cc[VMIN] = 1;
    new_term.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_term) == -1) {
        perror("tcsetattr");
        return EXIT_FAILURE;
    }

    terminal_changed = 1;
    atexit(restore_terminal);

    while (read(STDIN_FILENO, &c, 1) == 1) {

        /* Стрелочки: игнорируем и подаём сигнал */
        if (c == 27) {
            skip_escape();
            continue;
        }

        /* CTRL-D завершает программу в начале строки */
        if (c == 4) {
            if (len == line_start)
                break;

            beep();
            continue;
        }

        /* ERASE: Backspace */
        if (c == erase_char || c == 127 || c == 8) {
            erase_last();
            continue;
        }

        /* KILL: CTRL-U */
        if (c == kill_char || c == 21) {
            kill_line();
            continue;
        }

        /* CTRL-W */
        if (c == 23) {
            erase_word();
            continue;
        }

        /* Enter */
        if (c == '\r' || c == '\n') {
            putchar('\n');
            fflush(stdout);

            len = 0;
            line_start = 0;
            shown = 0;
            continue;
        }

        /* Непечатаемые символы */
        if (!isprint(c)) {
            beep();
            continue;
        }

        /* Добавляем символ */
        if (len < MAX_TEXT - 1) {
            text[len++] = (char)c;
            redraw();
        } else {
            beep();
        }
    }

    putchar('\n');
    fflush(stdout);

    return EXIT_SUCCESS;
}
