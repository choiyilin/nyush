#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_JOBS 100
#define MAX_CMD_LEN 1000

struct Job {
    pid_t pid;
    char cmd[MAX_CMD_LEN + 1];
};

static struct Job jobs[MAX_JOBS];
static int jobs_count = 0;

static void add_job(pid_t pid, const char *cmd)
{
    if (jobs_count >= MAX_JOBS) {
        return;
    }
    jobs[jobs_count].pid = pid;
    snprintf(jobs[jobs_count].cmd, sizeof(jobs[jobs_count].cmd), "%s", cmd);
    jobs_count++;
}

static void remove_job_at(int idx)
{
    for (int i = idx; i + 1 < jobs_count; i++) {
        jobs[i] = jobs[i + 1];
    }
    jobs_count--;
}

static int is_valid_index_arg(const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (int i = 0; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

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
        char original_cmd[MAX_CMD_LEN + 1];
        snprintf(original_cmd, sizeof(original_cmd), "%s", line);

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

        int pipe_count = 0;
        for (int i = 0; i < argc; i++) {
            if (strcmp(args[i], "|") == 0) {
                pipe_count++;
            }
        }

        if (pipe_count > 0) {
            int cmd_count = pipe_count + 1;
            char *cmd_args[128][128];
            int cmd_argc[128] = {0};
            char prog_paths[128][1024];
            char *infile = NULL;
            char *outfile = NULL;
            int append = 0;

            int cmd_idx = 0;
            for (int i = 0; i < argc; i++) {
                if (strcmp(args[i], "|") == 0) {
                    if (cmd_argc[cmd_idx] == 0) {
                        fprintf(stderr, "Error: invalid command\n");
                        goto next_cmd;
                    }
                    cmd_args[cmd_idx][cmd_argc[cmd_idx]] = NULL;
                    cmd_idx++;
                    if (cmd_idx >= cmd_count) {
                        fprintf(stderr, "Error: invalid command\n");
                        goto next_cmd;
                    }
                    continue;
                }

                cmd_args[cmd_idx][cmd_argc[cmd_idx]++] = args[i];
            }

            if (cmd_idx != cmd_count - 1 || cmd_argc[cmd_idx] == 0) {
                fprintf(stderr, "Error: invalid command\n");
                goto next_cmd;
            }

            // Parse redirections inside each pipeline segment.
            for (int c = 0; c < cmd_count; c++) {
                for (int i = 0; i < cmd_argc[c]; ) {
                    if (strcmp(cmd_args[c][i], "<") == 0) {
                        if (c != 0 || infile != NULL || i + 1 >= cmd_argc[c]) {
                            fprintf(stderr, "Error: invalid command\n");
                            goto next_cmd;
                        }
                        infile = cmd_args[c][i + 1];
                        for (int j = i; j + 2 <= cmd_argc[c]; j++) {
                            cmd_args[c][j] = cmd_args[c][j + 2];
                        }
                        cmd_argc[c] -= 2;
                        continue;
                    }

                    if (strcmp(cmd_args[c][i], ">") == 0 || strcmp(cmd_args[c][i], ">>") == 0) {
                        if (c != cmd_count - 1 || outfile != NULL || i + 1 >= cmd_argc[c]) {
                            fprintf(stderr, "Error: invalid command\n");
                            goto next_cmd;
                        }
                        append = (strcmp(cmd_args[c][i], ">>") == 0);
                        outfile = cmd_args[c][i + 1];
                        for (int j = i; j + 2 <= cmd_argc[c]; j++) {
                            cmd_args[c][j] = cmd_args[c][j + 2];
                        }
                        cmd_argc[c] -= 2;
                        continue;
                    }
                    i++;
                }

                if (cmd_argc[c] == 0) {
                    fprintf(stderr, "Error: invalid command\n");
                    goto next_cmd;
                }
                cmd_args[c][cmd_argc[c]] = NULL;
            }

            for (int i = 0; i < cmd_count; i++) {
                // Built ins cannot be piped.
                if (strcmp(cmd_args[i][0], "cd") == 0 || strcmp(cmd_args[i][0], "exit") == 0 ||
                    strcmp(cmd_args[i][0], "jobs") == 0 || strcmp(cmd_args[i][0], "fg") == 0) {
                    fprintf(stderr, "Error: invalid command\n");
                    goto next_cmd;
                }

                if (cmd_args[i][0][0] == '/' || strchr(cmd_args[i][0], '/') != NULL) {
                    snprintf(prog_paths[i], sizeof(prog_paths[i]), "%s", cmd_args[i][0]);
                } else {
                    snprintf(prog_paths[i], sizeof(prog_paths[i]), "/usr/bin/%s", cmd_args[i][0]);
                }
            }

            int pipes[127][2];
            for (int i = 0; i < cmd_count - 1; i++) {
                if (pipe(pipes[i]) < 0) {
                    perror("pipe");
                    goto next_cmd;
                }
            }

            pid_t pids[128];
            for (int i = 0; i < cmd_count; i++) {
                pids[i] = fork();
                if (pids[i] < 0) {
                    perror("fork");
                    for (int k = 0; k < cmd_count - 1; k++) {
                        close(pipes[k][0]);
                        close(pipes[k][1]);
                    }
                    for (int k = 0; k < i; k++) {
                        waitpid(pids[k], NULL, 0);
                    }
                    goto next_cmd;
                } else if (pids[i] == 0) {
                    signal(SIGINT, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);
                    signal(SIGTSTP, SIG_DFL);

                    if (i > 0) {
                        dup2(pipes[i - 1][0], STDIN_FILENO);
                    }
                    if (i < cmd_count - 1) {
                        dup2(pipes[i][1], STDOUT_FILENO);
                    }

                    for (int k = 0; k < cmd_count - 1; k++) {
                        close(pipes[k][0]);
                        close(pipes[k][1]);
                    }

                    if (i == 0 && infile != NULL) {
                        int fd = open(infile, O_RDONLY);
                        if (fd < 0) {
                            fprintf(stderr, "Error: invalid file\n");
                            _exit(1);
                        }
                        dup2(fd, STDIN_FILENO);
                        close(fd);
                    }
                    if (i == cmd_count - 1 && outfile != NULL) {
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

                    cmd_args[i][0] = prog_paths[i];
                    execv(prog_paths[i], cmd_args[i]);
                    fprintf(stderr, "Error: invalid program\n");
                    _exit(1);
                }
            }

            for (int i = 0; i < cmd_count - 1; i++) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
            for (int i = 0; i < cmd_count; i++) {
                int st;
                waitpid(pids[i], &st, WUNTRACED);
            }
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

        if (strcmp(args[0], "jobs") == 0) {
            if (argc != 1) {
                fprintf(stderr, "Error: invalid command\n");
                continue;
            }
            for (int i = 0; i < jobs_count; i++) {
                printf("[%d] %s\n", i + 1, jobs[i].cmd);
            }
            continue;
        }

        if (strcmp(args[0], "fg") == 0) {
            if (argc != 2) {
                fprintf(stderr, "Error: invalid command\n");
                continue;
            }
            if (!is_valid_index_arg(args[1])) {
                fprintf(stderr, "Error: invalid job\n");
                continue;
            }

            int idx = atoi(args[1]);
            if (idx <= 0 || idx > jobs_count) {
                fprintf(stderr, "Error: invalid job\n");
                continue;
            }

            idx -= 1;
            pid_t pid = jobs[idx].pid;
            char cmd_copy[MAX_CMD_LEN + 1];
            snprintf(cmd_copy, sizeof(cmd_copy), "%s", jobs[idx].cmd);
            remove_job_at(idx);

            if (kill(pid, SIGCONT) != 0) {
                continue;
            }

            int status;
            waitpid(pid, &status, WUNTRACED);
            if (WIFSTOPPED(status)) {
                add_job(pid, cmd_copy);
            }
            continue;
        }

        if (strcmp(args[0], "exit") == 0) {
            if (argc != 1) {
                fprintf(stderr, "Error: invalid command\n");
                continue;
            }
            if (jobs_count > 0) {
                fprintf(stderr, "Error: there are suspended jobs\n");
                continue;
            }
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
            waitpid(pid, &status, WUNTRACED);
            if (WIFSTOPPED(status)) {
                add_job(pid, original_cmd);
            }
        }

        next_cmd: ;

    }

    free(line);
    return 0;
}
