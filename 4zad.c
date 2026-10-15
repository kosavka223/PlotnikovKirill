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
