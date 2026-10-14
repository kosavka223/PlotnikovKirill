#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/ioctl.h>

struct node {
    char *text;
    struct node *next;
};

static volatile sig_atomic_t interrupted = 0;
static void on_signal(int sig) { interrupted = sig; }

static void free_list(struct node *head)
{
    while (head) {
        struct node *next = head->next;
        free(head->text);
        free(head);
        head = next;
    }
}

static int reserve(char **buf, size_t *cap, size_t need)
{
    if (need <= *cap) return 0;
    size_t new_cap = *cap ? *cap : 64;
    while (new_cap < need) {
        if (new_cap > (size_t)-1 / 2) return -1;
        new_cap *= 2;
    }
    char *p = realloc(*buf, new_cap);
    if (!p) return -1;
    *buf = p;
    *cap = new_cap;
    return 0;
}

/* One UTF-8 character is one cursor step (Cyrillic works too). */
static size_t prev_char(const char *s, size_t pos)
{
    if (!pos) return 0;
    pos--;
    while (pos && ((unsigned char)s[pos] & 0xc0) == 0x80) pos--;
    return pos;
}

static size_t next_char(const char *s, size_t len, size_t pos)
{
    if (pos == len) return len;
    pos++;
    while (pos < len && ((unsigned char)s[pos] & 0xc0) == 0x80) pos++;
    return pos;
}

static size_t chars(const char *s, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++)
        if (((unsigned char)s[i] & 0xc0) != 0x80) n++;
    return n;
}

static int write_all(const char *s, size_t n)
{
    while (n) {
        ssize_t r = write(STDOUT_FILENO, s, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        s += r;
        n -= (size_t)r;
    }
    return 0;
}

/* Screen position for a line, with one spare column to prevent auto-wrap. */
static size_t visual_row(size_t n, size_t width)
{
    return n ? (n - 1) / width : 0;
}

static size_t visual_col(size_t n, size_t width)
{
    return n ? ((n - 1) % width) + 1 : 0;
}

static void restore_line_origin(size_t cursor_row)
{
    char esc[64];
    write_all("\r", 1);
    if (cursor_row) {
        int n = snprintf(esc, sizeof(esc), "\033[%zuA", cursor_row);
        if (n > 0 && n < (int)sizeof(esc)) write_all(esc, (size_t)n);
    }
    write_all("\033[J", 3);
}

static int redraw(const char *buf, size_t len, size_t cursor,
                  size_t width, size_t *last_row)
{
    char esc[64];
    restore_line_origin(*last_row);
    size_t column = 0;
    for (size_t i = 0; i < len;) {
        if (column == width) {
            if (write_all("\r\n", 2) < 0) return -1;
            column = 0;
        }
        size_t end = next_char(buf, len, i);
        if (write_all(buf + i, end - i) < 0) return -1;
        column++;
        i = end;
    }
    size_t end_row = visual_row(chars(buf, len), width);
    size_t want = chars(buf, cursor);
    size_t wanted_row = visual_row(want, width);
    size_t wanted_col = visual_col(want, width);
    write_all("\r", 1);
    if (end_row > wanted_row) {
        int n = snprintf(esc, sizeof(esc), "\033[%zuA", end_row - wanted_row);
        if (n <= 0 || n >= (int)sizeof(esc) || write_all(esc, (size_t)n) < 0)
            return -1;
    }
    if (wanted_col) {
        int n = snprintf(esc, sizeof(esc), "\033[%zuC", wanted_col);
        if (n <= 0 || n >= (int)sizeof(esc) || write_all(esc, (size_t)n) < 0)
            return -1;
    }
    *last_row = wanted_row;
    return 0;
}

/* Escape sequences: arrows, Home/End, Delete. */
static int wait_byte(unsigned char *c)
{
    fd_set rd;
    struct timeval tv;
    FD_ZERO(&rd);
    FD_SET(STDIN_FILENO, &rd);
    tv.tv_sec = 0;
    tv.tv_usec = 50000;
    int ready = select(STDIN_FILENO + 1, &rd, NULL, NULL, &tv);
    if (ready != 1) return 0;
    return read(STDIN_FILENO, c, 1) == 1;
}

static int special_key(void)
{
    unsigned char c, d;
    if (!wait_byte(&c) || (c != '[' && c != 'O')) return 0;
    if (!wait_byte(&d)) return 0;
    if (d == 'A' || d == 'B' || d == 'C' || d == 'D' || d == 'H' || d == 'F')
        return d;
    if (c == '[' && d == '3' && wait_byte(&d) && d == '~') return 'X';
    return 0;
}

static struct node *previous(struct node *head, struct node *cur)
{
    struct node *p = head, *prev = NULL;
    while (p && p != cur) { prev = p; p = p->next; }
    return prev;
}

static int load_history(char **buf, size_t *cap, size_t *len,
                        size_t *cursor, const char *src)
{
    size_t n = strlen(src);
    if (n && src[n - 1] == '\n') n--;
    if (reserve(buf, cap, n + 1) < 0) return -1;
    memcpy(*buf, src, n);
    (*buf)[n] = '\0';
    *len = *cursor = n;
    return 0;
}

/* Return allocated line, or NULL on EOF/error. Ctrl+D on empty input = EOF. */
static char *read_line(struct node *head, struct node *tail, int *failed)
{
    struct termios original, edited;
    if (tcgetattr(STDIN_FILENO, &original) < 0) { *failed = 1; return NULL; }
    edited = original;
    edited.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    edited.c_cc[VMIN] = 1;
    edited.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &edited) < 0) { *failed = 1; return NULL; }

    char *buf = NULL, *draft = NULL;
    size_t cap = 0, len = 0, cursor = 0;
    size_t last_row = 0, width = 79;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 5)
        width = ws.ws_col - 1;
    struct node *history = NULL;
    int eof = 0;
    if (reserve(&buf, &cap, 64) < 0) { *failed = 1; goto done; }
    buf[0] = '\0';

    while (!interrupted) {
        unsigned char c;
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r == 0) { eof = 1; break; }
        if (r < 0) {
            if (errno == EINTR) continue;
            *failed = 1;
            break;
        }
        if (c == '\r' || c == '\n') {
            if (reserve(&buf, &cap, len + 2) < 0) { *failed = 1; break; }
            buf[len++] = '\n';
            buf[len] = '\0';
            break;
        }
        if (c == 4) { /* Ctrl+D */
            if (len == 0) { eof = 1; break; }
            if (cursor < len) {
                size_t end = next_char(buf, len, cursor);
                memmove(buf + cursor, buf + end, len - end + 1);
                len -= end - cursor;
            }
        } else if (c == 127 || c == 8) { /* Backspace */
            if (cursor) {
                size_t start = prev_char(buf, cursor);
                memmove(buf + start, buf + cursor, len - cursor + 1);
                len -= cursor - start;
                cursor = start;
            }
        } else if (c == 1) { /* Ctrl+A */
            cursor = 0;
        } else if (c == 5) { /* Ctrl+E */
            cursor = len;
        } else if (c == 27) { /* Esc */
            int key = special_key();
            if (key == 'D') cursor = prev_char(buf, cursor);
            if (key == 'C') cursor = next_char(buf, len, cursor);
            if (key == 'H') cursor = 0;
            if (key == 'F') cursor = len;
            if (key == 'X' && cursor < len) { /* Delete */
                size_t end = next_char(buf, len, cursor);
                memmove(buf + cursor, buf + end, len - end + 1);
                len -= end - cursor;
            }
            if (key == 'A' && (history || tail)) { /* Up */
                if (!history) {
                    draft = strdup(buf);
                    if (!draft) { *failed = 1; break; }
                    history = tail;
                } else {
                    struct node *p = previous(head, history);
                    if (p) history = p;
                }
                if (load_history(&buf, &cap, &len, &cursor, history->text) < 0) {
                    *failed = 1; break;
                }
            }
            if (key == 'B' && history) { /* Down */
                history = history->next;
                const char *src = history ? history->text : (draft ? draft : "");
                if (load_history(&buf, &cap, &len, &cursor, src) < 0) {
                    *failed = 1; break;
                }
                if (!history) { free(draft); draft = NULL; }
            }
        } else if (c >= 32 && c != 127) {
            if (reserve(&buf, &cap, len + 2) < 0) { *failed = 1; break; }
            memmove(buf + cursor + 1, buf + cursor, len - cursor + 1);
            buf[cursor++] = (char)c;
            len++;
        }
        if (redraw(buf, len, cursor, width, &last_row) < 0) { *failed = 1; break; }
    }

    if (interrupted) *failed = 1;
    if (!*failed && !eof && len > 0 && buf[len - 1] != '\n') {
        if (reserve(&buf, &cap, len + 2) < 0) *failed = 1;
        else { buf[len++] = '\n'; buf[len] = '\0'; }
    }
    if (eof && len == 0) { free(buf); buf = NULL; }

done:
    tcsetattr(STDIN_FILENO, TCSANOW, &original);
    restore_line_origin(last_row);
    if (!*failed && buf) write_all(buf, strlen(buf));
    free(draft);
    if (*failed) { free(buf); buf = NULL; }
    return buf;
}

int main(void)
{
    struct node *head = NULL, *tail = NULL;
    char *line = NULL;
    size_t line_cap = 0;
    int failed = 0, interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    struct sigaction act, old_int, old_term, old_hup, old_tstp;
    if (interactive) {
        memset(&act, 0, sizeof(act));
        act.sa_handler = on_signal;
        sigemptyset(&act.sa_mask);
        sigaction(SIGINT, &act, &old_int);
        sigaction(SIGTERM, &act, &old_term);
        sigaction(SIGHUP, &act, &old_hup);
        sigaction(SIGTSTP, &act, &old_tstp);
    }

    for (;;) {
        if (interactive) line = read_line(head, tail, &failed);
        else {
            ssize_t n = getline(&line, &line_cap, stdin);
            if (n < 0) { free(line); line = NULL; }
        }
        if (!line) break;
        if (line[0] == '.') { free(line); line = NULL; break; }

        struct node *item = malloc(sizeof(*item));
        if (!item) { perror("malloc"); failed = 1; free(line); line = NULL; break; }
        item->text = line;
        item->next = NULL;
        if (tail) tail->next = item;
        else head = item;
        tail = item;
        line = NULL;
    }

    if (interactive) {
        sigaction(SIGINT, &old_int, NULL);
        sigaction(SIGTERM, &old_term, NULL);
        sigaction(SIGHUP, &old_hup, NULL);
        sigaction(SIGTSTP, &old_tstp, NULL);
    }
    if (interrupted) failed = 1;
    if (!failed) for (struct node *p = head; p; p = p->next) fputs(p->text, stdout);
    if (ferror(stdout)) failed = 1;
    free_list(head);
    if (failed && !interrupted) fprintf(stderr, "input or memory error\n");
    return interrupted == SIGINT ? 130 : (failed ? EXIT_FAILURE : EXIT_SUCCESS);
}
