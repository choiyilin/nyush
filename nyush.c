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
            basename++; //skip /
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

        /* Tokenize the line into arguments using strtok_r. */
        char *args[128];
        int argc = 0;
        char *saveptr;
        char *token = strtok_r(line, " ", &saveptr);
        while (token != NULL && argc < 127) {
            args[argc++] = token;
            token = strtok_r(NULL, " ", &saveptr);
        }
        args[argc] = NULL;

        if (argc == 0) {
            continue;
        }

        /* Resolve program path based on the command name (args[0]):
         *   - starts with '/' -> absolute path
         *   - contains '/'   -> relative path
         *   - otherwise       -> prepend /usr/bin/
         */
        char prog[1024];
        if (args[0][0] == '/' || strchr(args[0], '/') != NULL) {
            snprintf(prog, sizeof(prog), "%s", args[0]);
        } else {
            snprintf(prog, sizeof(prog), "/usr/bin/%s", args[0]);
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            continue;
        } else if (pid == 0) {
            args[0] = prog;
            execv(prog, args);
            fprintf(stderr, "Error: invalid program\n");
            _exit(1);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }

    }

    free(line);
    return 0;
}
