#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

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
    /* Shell ignores these signals; children will reset to default. */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

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

        /* Built-in: cd <dir> */
        if (strcmp(args[0], "cd") == 0) {
            if (argc != 2) {
                fprintf(stderr, "Error: invalid command\n");
            } else if (chdir(args[1]) != 0) {
                fprintf(stderr, "Error: invalid directory\n");
            }
            continue;
        }

        //exit built in here
        if (strcmp(args[0], "exit") == 0) {
            if (argc != 1) {
                fprintf(stderr, "Error: invalid command\n");
                continue;
            }
            //need to impliment job ctrl
            free(line);
            exit(0);
        }

        // Parse output redirection: > or >> followed by a filename.         
        char *outfile = NULL;
        int append = 0;
        for (int i = 0; i < argc; i++) {
            if (strcmp(args[i], ">>") == 0 || strcmp(args[i], ">") == 0) {
                append = (strcmp(args[i], ">>") == 0);
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: invalid command\n");
                    outfile = NULL;
                    goto next_cmd;
                }
                outfile = args[i + 1];
                for (int j = i; j + 2 <= argc; j++) {
                    args[j] = args[j + 2];
                }
                argc -= 2;
                break;
            }
        }

        if (argc == 0) {
            fprintf(stderr, "Error: invalid command\n");
            continue;
        }

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
            // reset sig handlers to default in child
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);

            // output redirection in child
            if (outfile != NULL) {
                int flags = O_WRONLY | O_CREAT;
                flags |= append ? O_APPEND : O_TRUNC;
                int fd = open(outfile, flags, 0644);
                if (fd < 0) {
                    fprintf(stderr, "Error: invalid file\n");
                    _exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            args[0] = prog;
            execv(prog, args);
            fprintf(stderr, "Error: invalid program\n");
            _exit(1);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }

        next_cmd: ;

    }

    free(line);
    return 0;
}
