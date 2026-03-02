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

        int pipe_idx = -1;
        for (int i = 0; i < argc; i++) {
            if (strcmp(args[i], "|") == 0) {
                if (pipe_idx != -1) {
                    fprintf(stderr, "Error: invalid command\n");
                    goto next_cmd;
                }
                pipe_idx = i;
            }
        }

        if (pipe_idx != -1) {
            if (pipe_idx == 0 || pipe_idx == argc - 1) {
                fprintf(stderr, "Error: invalid command\n");
                goto next_cmd;
            }

            char *left_args[128];
            char *right_args[128];
            int left_argc = 0;
            int right_argc = 0;

            for (int i = 0; i < pipe_idx; i++) {
                left_args[left_argc++] = args[i];
            }
            left_args[left_argc] = NULL;

            for (int i = pipe_idx + 1; i < argc; i++) {
                right_args[right_argc++] = args[i];
            }
            right_args[right_argc] = NULL;

//built-ins cannot be piped
            if (strcmp(left_args[0], "cd") == 0 || strcmp(left_args[0], "exit") == 0 ||
                strcmp(right_args[0], "cd") == 0 || strcmp(right_args[0], "exit") == 0) {
                fprintf(stderr, "Error: invalid command\n");
                goto next_cmd;
            }

//no redirection
            for (int i = 0; i < left_argc; i++) {
                if (strcmp(left_args[i], "<") == 0 || strcmp(left_args[i], ">") == 0 ||
                    strcmp(left_args[i], ">>") == 0 || strcmp(left_args[i], "|") == 0) {
                    fprintf(stderr, "Error: invalid command\n");
                    goto next_cmd;
                }
            }
            for (int i = 0; i < right_argc; i++) {
                if (strcmp(right_args[i], "<") == 0 || strcmp(right_args[i], ">") == 0 ||
                    strcmp(right_args[i], ">>") == 0 || strcmp(right_args[i], "|") == 0) {
                    fprintf(stderr, "Error: invalid command\n");
                    goto next_cmd;
                }
            }

            char left_prog[1024];
            char right_prog[1024];
            if (left_args[0][0] == '/' || strchr(left_args[0], '/') != NULL) {
                snprintf(left_prog, sizeof(left_prog), "%s", left_args[0]);
            } else {
                snprintf(left_prog, sizeof(left_prog), "/usr/bin/%s", left_args[0]);
            }
            if (right_args[0][0] == '/' || strchr(right_args[0], '/') != NULL) {
                snprintf(right_prog, sizeof(right_prog), "%s", right_args[0]);
            } else {
                snprintf(right_prog, sizeof(right_prog), "/usr/bin/%s", right_args[0]);
            }

            int pipefd[2];
            if (pipe(pipefd) < 0) {
                perror("pipe");
                goto next_cmd;
            }

            pid_t p1 = fork();
            if (p1 < 0) {
                perror("fork");
                close(pipefd[0]);
                close(pipefd[1]);
                goto next_cmd;
            } else if (p1 == 0) {
                signal(SIGINT, SIG_DFL);
                signal(SIGQUIT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);

                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);

                left_args[0] = left_prog;
                execv(left_prog, left_args);
                fprintf(stderr, "Error: invalid program\n");
                _exit(1);
            }

            pid_t p2 = fork();
            if (p2 < 0) {
                perror("fork");
                close(pipefd[0]);
                close(pipefd[1]);
                waitpid(p1, NULL, 0);
                goto next_cmd;
            } else if (p2 == 0) {
                signal(SIGINT, SIG_DFL);
                signal(SIGQUIT, SIG_DFL);
                signal(SIGTSTP, SIG_DFL);

                dup2(pipefd[0], STDIN_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);

                right_args[0] = right_prog;
                execv(right_prog, right_args);
                fprintf(stderr, "Error: invalid program\n");
                _exit(1);
            }

            close(pipefd[0]);
            close(pipefd[1]);
            waitpid(p1, NULL, 0);
            waitpid(p2, NULL, 0);
            goto next_cmd;
        }

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

        // Parse redirection tokens: <, >, >>.
        char *infile = NULL;
        char *outfile = NULL;
        int append = 0;
        for (int i = 0; i < argc; ) {
            if (strcmp(args[i], "<") == 0 ||
                strcmp(args[i], ">") == 0 ||
                strcmp(args[i], ">>") == 0) {
                if (i + 1 >= argc) {
                    fprintf(stderr, "Error: invalid command\n");
                    goto next_cmd;
                }

                if (strcmp(args[i], "<") == 0) {
                    if (infile != NULL) {
                        fprintf(stderr, "Error: invalid command\n");
                        goto next_cmd;
                    }
                    infile = args[i + 1];
                } else {
                    if (outfile != NULL) {
                        fprintf(stderr, "Error: invalid command\n");
                        goto next_cmd;
                    }
                    append = (strcmp(args[i], ">>") == 0);
                    outfile = args[i + 1];
                }

                for (int j = i; j + 2 <= argc; j++) {
                    args[j] = args[j + 2];
                }
                argc -= 2;
                continue;
            }
            i++;
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

            // input redirection in child
            if (infile != NULL) {
                int fd = open(infile, O_RDONLY);
                if (fd < 0) {
                    fprintf(stderr, "Error: invalid file\n");
                    _exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

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
