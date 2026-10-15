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
