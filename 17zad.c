#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <errno.h>
#include <signal.h>

#define WIDTH 40
#define MAX_ROWS 1024

#define CTRL_D 4
#define CTRL_G 7
#define CTRL_W 23

enum {
    KEY_UP = 256,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_UNKNOWN
};

static char lines[MAX_ROWS][WIDTH + 2];

static size_t row_count = 1;
static size_t row = 0;
static size_t col = 0;

static size_t first_visible = 0;
static size_t shown_rows = 1;
static size_t shown_cursor_row = 0;
static size_t screen_rows = 23;

static struct termios old_termios;
static volatile sig_atomic_t stopping = 0;

static int pending = -1;


/* Восстановление настроек терминала */

static void restore_terminal(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
}


/* Обработка завершения программы */

static void stop_handler(int sig)
{
    (void)sig;
    stopping = 1;
}


/* Звуковой сигнал */

static void beep(void)
{
    putchar(CTRL_G);
    fflush(stdout);
}


/* Перерисовка текста */

static void redraw(void)
{
    size_t i, cursor_row;

    putchar('\r');

    if (shown_cursor_row > 0)
        printf("\033[%zuA", shown_cursor_row);

    fputs("\033[J", stdout);

    if (row < first_visible)
        first_visible = row;

    if (row - first_visible >= screen_rows)
        first_visible = row - screen_rows + 1;

    shown_rows = row_count - first_visible;

    if (shown_rows > screen_rows)
        shown_rows = screen_rows;

    for (i = 0; i < shown_rows; ++i) {

        if (i != 0)
            fputs("\r\n", stdout);

        fputs(lines[first_visible + i], stdout);
    }

    cursor_row = row - first_visible;

    if (shown_rows - 1 > cursor_row)
        printf("\033[%zuA",
               shown_rows - 1 - cursor_row);

    putchar('\r');

    if (col > 0)
        printf("\033[%zuC", col);

    shown_cursor_row = cursor_row;

    fflush(stdout);
}


/* Добавление новой строки */

static void insert_row(void)
{
    if (row + 1 < row_count) {

        memmove(
            lines[row + 2],
            lines[row + 1],
            (row_count - row - 1) * sizeof lines[0]
        );
    }

    lines[row + 1][0] = '\0';

    ++row_count;
}


/* Разделение строки */

static void split_row(size_t at)
{
    insert_row();

    strcpy(lines[row + 1], lines[row] + at);

    lines[row][at] = '\0';
}


/* Вставка символа */

static void insert_char(int ch)
{
    char *s = lines[row];

    size_t len = strlen(s);
    size_t split;

    if (len == WIDTH && row_count == MAX_ROWS) {
        beep();
        return;
    }

    memmove(
        s + col + 1,
        s + col,
        len - col + 1
    );

    s[col++] = (char)ch;

    ++len;

    /* Перенос слова при превышении 40 символов */

    if (len > WIDTH) {

        split = WIDTH;

        if (s[WIDTH] != ' ') {

            while (split > 0 &&
                   s[split - 1] != ' ') {

                --split;
            }

            /* Если слово длиннее 40 символов */

            if (split == 0)
                split = WIDTH;
        }

        split_row(split);

        if (col > split) {

            ++row;
            col -= split;
        }
    }
}


/* Удаление символа перед курсором */

static void erase_char(void)
{
    char *s = lines[row];

    if (col == 0) {
        beep();
        return;
    }

    memmove(
        s + col - 1,
        s + col,
        strlen(s + col) + 1
    );

    --col;
}


/* Удаление слова через CTRL-W */

static void erase_word(void)
{
    char *s = lines[row];

    size_t start = col;

    /* Удаляем пробелы */

    while (start > 0 &&
           s[start - 1] == ' ') {

        --start;
    }

    /* Удаляем последнее слово */

    while (start > 0 &&
           s[start - 1] != ' ') {

        --start;
    }

    if (start == col) {
        beep();
        return;
    }

    memmove(
        s + start,
        s + col,
        strlen(s + col) + 1
    );

    col = start;
}


/* Обработка стрелочек */

static void move_cursor(int key)
{
    size_t len = strlen(lines[row]);

    switch (key) {

        /* Стрелка влево */

        case KEY_LEFT:

            if (col > 0) {

                --col;

            } else if (row > 0) {

                --row;
                col = strlen(lines[row]);

            } else {

                beep();
            }

            break;


        /* Стрелка вправо */

        case KEY_RIGHT:

            if (col < len) {

                ++col;

            } else if (row + 1 < row_count) {

                ++row;
                col = 0;

            } else {

                beep();
            }

            break;


        /* Стрелка вверх */

        case KEY_UP:

            if (row > 0)
                --row;
            else
                beep();

            break;


        /* Стрелка вниз */

        case KEY_DOWN:

            if (row + 1 < row_count)
                ++row;
            else
                beep();

            break;
    }

    len = strlen(lines[row]);

    if (col > len)
        col = len;
}


/* Чтение одного символа */

static int read_byte(int timeout)
{
    unsigned char ch;

    ssize_t n;

    struct pollfd pfd;

    int rc;

    if (stopping)
        return -1;

    if (timeout >= 0) {

        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;

        do {

            rc = poll(&pfd, 1, timeout);

        } while (rc < 0 &&
                 errno == EINTR &&
                 !stopping);

        if (rc <= 0)
            return -1;
    }

    do {

        n = read(STDIN_FILENO, &ch, 1);

    } while (n < 0 &&
             errno == EINTR &&
             !stopping);

    return n == 1 ? ch : -1;
}


/* Распознавание обычных клавиш и стрелочек */

static int read_key(void)
{
    int ch, next, i;

    if (pending >= 0) {

        ch = pending;
        pending = -1;

    } else {

        ch = read_byte(-1);
    }

    /* Обычная клавиша */

    if (ch != 27)
        return ch;

    /* ESC-последовательность */

    next = read_byte(150);

    if (next != '[' && next != 'O') {

        if (next >= 0)
            pending = next;

        return KEY_UNKNOWN;
    }

    ch = read_byte(150);

    switch (ch) {

        case 'A':
            return KEY_UP;

        case 'B':
            return KEY_DOWN;

        case 'C':
            return KEY_RIGHT;

        case 'D':
            return KEY_LEFT;
    }

    /* Другие управляющие последовательности */

    if (ch >= 0x20 && ch <= 0x3f) {

        for (i = 0; i < 16; ++i) {

            ch = read_byte(150);

            if (ch < 0 ||
                (ch >= 0x40 && ch <= 0x7e)) {

                break;
            }
        }
    }

    return KEY_UNKNOWN;
}


/* Главная функция */

int main(void)
{
    struct termios raw;
    struct winsize ws;
    struct sigaction sa;

    int ch;

    /* Проверяем, что программа запущена в терминале */

    if (!isatty(STDIN_FILENO) ||
        !isatty(STDOUT_FILENO)) {

        fprintf(stderr, "Run in a terminal.\n");

        return 1;
    }


    /* Получаем размеры терминала */

    if (ioctl(STDOUT_FILENO,
              TIOCGWINSZ, &ws) == 0) {

        if (ws.ws_col > 0 &&
            ws.ws_col <= WIDTH) {

            fprintf(
                stderr,
                "Use a terminal at least 41 columns wide.\n"
            );

            return 1;
        }

        if (ws.ws_row > 1)
            screen_rows = ws.ws_row - 1;
    }


    /* Сохраняем настройки терминала */

    if (tcgetattr(
            STDIN_FILENO,
            &old_termios
        ) == -1) {

        perror("tcgetattr");

        return 1;
    }

    raw = old_termios;


    /* Отключаем эхо и канонический режим */

    raw.c_lflag &= ~(
        ICANON |
        ECHO |
        ECHONL |
        ISIG |
        IEXTEN
    );

    raw.c_iflag &= ~(
        IXON |
        IXOFF |
        ICRNL |
        INLCR |
        IGNCR
    );

    raw.c_oflag &= ~OPOST;

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;


    /* Восстановление терминала при выходе */

    if (atexit(restore_terminal) != 0)
        return 1;


    /* Обработка сигналов завершения */

    memset(&sa, 0, sizeof sa);

    sa.sa_handler = stop_handler;

    sigemptyset(&sa.sa_mask);

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);


    /* Применяем новые настройки */

    if (tcsetattr(
            STDIN_FILENO,
            TCSANOW,
            &raw
        ) == -1) {

        perror("tcsetattr");

        return 1;
    }


    fputs(
        "Line editor: 40 columns. "
        "Arrows, Ctrl-U, Ctrl-W, Ctrl-D.\r\n",
        stdout
    );

    fflush(stdout);


    /* Основной цикл редактора */

    while (!stopping &&
           (ch = read_key()) >= 0) {


        /* Стрелочки */

        if (ch >= KEY_UP && ch <= KEY_RIGHT) {

            move_cursor(ch);
        }


        /* CTRL-D: выход в начале строки */

        else if (ch == CTRL_D) {

            if (col == 0)
                break;

            beep();

            continue;
        }


        /* CTRL-W: удаление слова */

        else if (ch == CTRL_W) {

            erase_word();
        }


        /* Backspace: удаление символа */

        else if (
            ch == old_termios.c_cc[VERASE]
        ) {

            erase_char();
        }


        /* CTRL-U: удаление только текущей строки */

        else if (
            ch == old_termios.c_cc[VKILL]
        ) {

            lines[row][0] = '\0';

            col = 0;
        }


        /* Enter: новая строка */

        else if (ch == '\r' || ch == '\n') {

            if (row_count == MAX_ROWS) {

                beep();

                continue;
            }

            split_row(col);

            ++row;

            col = 0;
        }


        /* Обычные печатные символы */

        else if (ch >= 32 && ch <= 126) {

            insert_char(ch);
        }


        /* Непечатаемые символы: звуковой сигнал */

        else {

            beep();

            continue;
        }


        /* Обновляем экран */

        redraw();
    }


    /* Перемещаем курсор под текст перед выходом */

    putchar('\r');

    if (shown_rows - 1 > shown_cursor_row) {

        printf(
            "\033[%zuB",
            shown_rows - 1 - shown_cursor_row
        );
    }

    fputs("\r\n", stdout);

    fflush(stdout);

    return 0;
}
