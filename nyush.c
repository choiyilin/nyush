#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

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

        /* Resolve the program path:
         *   - starts with '/' -> absolute path, use as-is
         *   - contains '/'   -> relative path, use as-is
         *   - otherwise       -> prepend /usr/bin/
         */
        char prog[1024];
        if (line[0] == '/' || strchr(line, '/') != NULL) {
            snprintf(prog, sizeof(prog), "%s", line);
        } else {
            snprintf(prog, sizeof(prog), "/usr/bin/%s", line);
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        } else if (pid == 0) {
            /* Child: exec the program (no arguments yet). */
            char *argv[] = {prog, NULL};
            execv(prog, argv);
            fprintf(stderr, "Error: invalid program\n");
            _exit(1);
        } else {
            /* Parent: wait for the child to finish. */
            int status;
            waitpid(pid, &status, 0);
        }

    }

    free(line);
    return 0;
}
