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

/* Linked list required by assignment 4. */
struct node {
    char *text;
    struct node *next;
};

static volatile sig_atomic_t stop_signal = 0;
static void signal_handler(int signum) { stop_signal = signum; }

static void release_list(struct node *head)
{
    while (head) {
        struct node *next = head->next;
        free(head->text);
        free(head);
        head = next;
    }
}

static int ensure_space(char **p, size_t *cap, size_t need)
{
    if (need <= *cap) return 0;
    size_t n = *cap ? *cap : 64;
    while (n < need) {
        if (n > (size_t)-1 / 2) return -1;
        n *= 2;
    }
    char *q = realloc(*p, n);
    if (!q) return -1;
    *p = q;
    *cap = n;
    return 0;
}

static int output(const char *data, size_t n)
{
    while (n) {
        ssize_t w = write(STDOUT_FILENO, data, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        data += w;
        n -= (size_t)w;
    }
    return 0;
}

/* Treat a UTF-8 code point as a cursor step; ASCII works as usual. */
static size_t next_cp(const char *s, size_t len, size_t pos)
{
    if (pos >= len) return len;
    ++pos;
    while (pos < len && ((unsigned char)s[pos] & 0xc0) == 0x80) ++pos;
    return pos;
}

static size_t previous_cp(const char *s, size_t pos)
{
    if (!pos) return 0;
    --pos;
    while (pos && ((unsigned char)s[pos] & 0xc0) == 0x80) --pos;
    return pos;
}

static size_t cp_count(const char *s, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i = next_cp(s, len, i)) ++n;
    return n;
}

static size_t byte_at_cp(const char *s, size_t len, size_t index)
{
    size_t i = 0;
    while (index && i < len) { i = next_cp(s, len, i); --index; }
    return i;
}

static size_t terminal_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
        return ws.ws_col;
    return 80;
}

/* Redraw in one terminal row. Long lines scroll horizontally, so they
   never corrupt the screen by wrapping over the terminal's bottom. */
static int redraw(const char *buf, size_t len, size_t cursor, size_t *first_cp)
{
    size_t cols = terminal_width();
    size_t visible = cols - 6;
    size_t total = cp_count(buf, len);
    size_t current = cp_count(buf, cursor);
    if (current < *first_cp) *first_cp = current;
    if (current - *first_cp > visible) *first_cp = current - visible;
    if (*first_cp > total) *first_cp = total;
    size_t end_cp = *first_cp + visible;
    if (end_cp > total) end_cp = total;
    size_t begin = byte_at_cp(buf, len, *first_cp);
    size_t end = byte_at_cp(buf, len, end_cp);
    char esc[64];

    if (output("\r\033[2K> ", 7) < 0) return -1;
    if (output(*first_cp ? "<" : " ", 1) < 0) return -1;
    if (output(buf + begin, end - begin) < 0) return -1;
    if (output(end_cp < total ? ">" : " ", 1) < 0) return -1;
    /* Cursor positioning uses absolute column, from 1. */
    int n = snprintf(esc, sizeof(esc), "\r\033[%zuC", 3 + current - *first_cp);
    if (n < 0 || n >= (int)sizeof(esc)) return -1;
    return output(esc, (size_t)n);
}

/* Read the rest of a terminal escape sequence without getting stuck. */
static int timed_byte(unsigned char *out)
{
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeout.tv_sec = 0;
    timeout.tv_usec = 80000;
    int r = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
    return r == 1 && read(STDIN_FILENO, out, 1) == 1;
}

/* A/B/C/D arrows, H/F home/end, X delete, 0 unknown. */
static int decode_escape(void)
{
    unsigned char prefix, ch;
    char seq[24];
    size_t n = 0;
    if (!timed_byte(&prefix) || (prefix != '[' && prefix != 'O')) return 0;
    while (n < sizeof(seq) - 1 && timed_byte(&ch)) {
        seq[n++] = (char)ch;
        if (ch >= 0x40 && ch <= 0x7e) break;
    }
    if (n == 0) return 0;
    ch = (unsigned char)seq[n - 1];
    if (ch == 'A' || ch == 'B' || ch == 'C' || ch == 'D' || ch == 'H' || ch == 'F')
        return ch;
    if (prefix == '[' && ch == '~' && seq[0] == '3') return 'X';
    return 0;
}

static struct node *previous_node(struct node *head, struct node *at)
{
    struct node *before = NULL;
    for (struct node *p = head; p && p != at; p = p->next) before = p;
    return before;
}

static int replace_buffer(char **buf, size_t *cap, size_t *len,
                          size_t *cursor, const char *line)
{
    size_t n = strlen(line);
    if (n && line[n - 1] == '\n') --n;
    if (ensure_space(buf, cap, n + 1) < 0) return -1;
    memcpy(*buf, line, n);
    (*buf)[n] = '\0';
    *cursor = *len = n;
    return 0;
}

/* Terminal mode for editing, regular getline() for redirected input. */
static char *interactive_line(struct node *head, struct node *tail, int *error)
{
    struct termios old, raw;
    if (tcgetattr(STDIN_FILENO, &old) < 0) { *error = 1; return NULL; }
    raw = old;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) { *error = 1; return NULL; }

    char *buf = NULL, *draft = NULL;
    size_t cap = 0, len = 0, cursor = 0, first_cp = 0;
    struct node *history = NULL;
    int eof = 0, enter = 0;
    if (ensure_space(&buf, &cap, 64) < 0) { *error = 1; goto cleanup; }
    buf[0] = '\0';
    if (redraw(buf, 0, 0, &first_cp) < 0) { *error = 1; goto cleanup; }

    while (!stop_signal) {
        unsigned char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n == 0) { eof = 1; break; }
        if (n < 0) {
            if (errno == EINTR) continue;
            *error = 1;
            break;
        }
        if (c == '\n' || c == '\r') { enter = 1; break; }
        if (c == 4 && len == 0) { eof = 1; break; } /* Ctrl+D */
        if (c == 127 || c == 8) { /* Backspace */
            if (cursor) {
                size_t begin = previous_cp(buf, cursor);
                memmove(buf + begin, buf + cursor, len - cursor + 1);
                len -= cursor - begin;
                cursor = begin;
            }
        } else if (c == 1) cursor = 0; /* Ctrl+A */
        else if (c == 5) cursor = len;   /* Ctrl+E */
        else if (c == 27) {
            int key = decode_escape();
            if (key == 'D') cursor = previous_cp(buf, cursor);
            else if (key == 'C') cursor = next_cp(buf, len, cursor);
            else if (key == 'H') cursor = 0;
            else if (key == 'F') cursor = len;
            else if (key == 'X' && cursor < len) {
                size_t end = next_cp(buf, len, cursor);
                memmove(buf + cursor, buf + end, len - end + 1);
                len -= end - cursor;
            } else if (key == 'A' && (history || tail)) {
                if (!history) {
                    draft = strdup(buf);
                    if (!draft) { *error = 1; break; }
                    history = tail;
                } else {
                    struct node *p = previous_node(head, history);
                    if (p) history = p;
                }
                if (replace_buffer(&buf, &cap, &len, &cursor, history->text) < 0) {
                    *error = 1; break;
                }
            } else if (key == 'B' && history) {
                history = history->next;
                const char *source = history ? history->text : (draft ? draft : "");
                if (replace_buffer(&buf, &cap, &len, &cursor, source) < 0) {
                    *error = 1; break;
                }
                if (!history) { free(draft); draft = NULL; }
            }
        } else if (c == 4) { /* Ctrl+D deletes character under cursor */
            if (cursor < len) {
                size_t end = next_cp(buf, len, cursor);
                memmove(buf + cursor, buf + end, len - end + 1);
                len -= end - cursor;
            }
        } else if (c >= 32 && c != 127) {
            if (len >= (size_t)-2 || ensure_space(&buf, &cap, len + 2) < 0) {
                *error = 1; break;
            }
            memmove(buf + cursor + 1, buf + cursor, len - cursor + 1);
            buf[cursor++] = (char)c;
            ++len;
        }
        if (redraw(buf, len, cursor, &first_cp) < 0) { *error = 1; break; }
    }
    if (stop_signal) *error = 1;
    if (enter && !*error) {
        if (ensure_space(&buf, &cap, len + 2) < 0) *error = 1;
        else {
            buf[len++] = '\n';
            buf[len] = '\0';
        }
    }
    if (eof && len == 0) { free(buf); buf = NULL; }

cleanup:
    /* Restore canonical input before leaving, even on error or Ctrl+C. */
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    output("\r\033[2K", 5);
    if (!*error && buf) {
        output("> ", 2);
        output(buf, strlen(buf));
    }
    free(draft);
    if (*error) { free(buf); buf = NULL; }
    return buf;
}

int main(void)
{
    struct node *head = NULL, *tail = NULL;
    char *line = NULL;
    size_t getline_cap = 0;
    int error = 0;
    int interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    struct sigaction sa, old_int, old_term, old_hup;

    if (interactive) {
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = signal_handler;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGINT, &sa, &old_int) == -1 ||
            sigaction(SIGTERM, &sa, &old_term) == -1 ||
            sigaction(SIGHUP, &sa, &old_hup) == -1) {
            perror("sigaction");
            return EXIT_FAILURE;
        }
    }
    while (!stop_signal) {
        if (interactive) line = interactive_line(head, tail, &error);
        else {
            ssize_t n = getline(&line, &getline_cap, stdin);
            if (n < 0) { free(line); line = NULL; }
        }
        if (!line) break;
        if (line[0] == '.') { free(line); line = NULL; break; }
        struct node *item = malloc(sizeof(*item));
        if (!item) { perror("malloc"); error = 1; free(line); line = NULL; break; }
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
    }
    if (!error && !stop_signal)
        for (struct node *p = head; p; p = p->next) fputs(p->text, stdout);
    if (ferror(stdout)) error = 1;
    release_list(head);
    if (error && !stop_signal) fprintf(stderr, "input error\n");
    return stop_signal == SIGINT ? 130 : (error ? EXIT_FAILURE : EXIT_SUCCESS);
}
