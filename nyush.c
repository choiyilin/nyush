#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * print_prompt - prints the shell prompt in the format: [nyush basename]$ 
 * If cwd is "/", basename is "/". Otherwise it's the last component.
 */
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
    print_prompt();
    return 0;
}
