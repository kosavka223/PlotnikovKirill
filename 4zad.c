#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE 1024

struct node {
    char *text;
    struct node *next;
};

static void free_list(struct node *head)
{
    while (head != NULL) {
        struct node *next = head->next;
        free(head->text);
        free(head);
        head = next;
    }
}

int main(void)
{
    char buf[MAX_LINE];
    struct node *head = NULL;
    struct node *tail = NULL;

    while (fgets(buf, sizeof(buf), stdin) != NULL) {
        struct node *item;
        size_t len;

        if (buf[0] == '.')
            break;

        len = strlen(buf);

        item = malloc(sizeof(*item));
        if (item == NULL) {
            perror("malloc");
            free_list(head);
            return EXIT_FAILURE;
        }

        item->text = malloc(len + 1);
        if (item->text == NULL) {
            perror("malloc");
            free(item);
            free_list(head);
            return EXIT_FAILURE;
        }

        memcpy(item->text, buf, len + 1);
        item->next = NULL;

        if (head == NULL)
            head = item;
        else
            tail->next = item;
        tail = item;
    }

    for (struct node *p = head; p != NULL; p = p->next)
        printf("%s", p->text);

    free_list(head);
    return 0;
}
