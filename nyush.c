#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_prompt(void)
{
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        exit(1);
    }

    const char *basename;
    if (strcmp(cwd, "/") == 0) {
        basename = "/";
    } else {
        basename = strrchr(cwd, '/');
        if (basename != NULL) {
            basename++; /* skip the '/' */
        } else {
            basename = cwd;
        }
    }

    printf("[nyush %s]$ ", basename);
    fflush(stdout);
}

int main(void)
{
    char *line = NULL;
    size_t len = 0;

    while (1) {
        print_prompt();

        if (getline(&line, &len, stdin) == -1) {
            break; 
        }

        line[strcspn(line, "\n")] = '\0';

        if (line[0] == '\0') {
            continue;
        }

    }

    free(line);
    return 0;
}
