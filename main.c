#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

sig_atomic_t should_wait = 0;

void sigchld_handler(int sig)
{
    should_wait = 1;
}

char **read_command(void)
{
    size_t buf_size = 1, end = 0, tok_size, num_args = 0;
    char *buffer = NULL;
    char *tok, *tokend, *arg;
    char **arglist = NULL;
    buffer = malloc(buf_size);
    if (!buffer)
        abort();
    buffer[0] = 0;
    while (1) {
        buf_size += 10;
        buffer = realloc(buffer, buf_size);
        if (!buffer || buf_size > INT_MAX)
            abort(); // TODO make better
        while (end < buf_size - 1) {
            if (!fgets(buffer + end, buf_size - end, stdin))
                goto done;
            end = strlen(buffer);
            if (end && buffer[end - 1] == '\n')
                goto done;
        }
    }
done:
    tok = buffer;
    while (tok[0]) {
        tokend = strpbrk(tok, " \t\f\r\v\n");
        if (!tokend)
            tokend = tok + strlen(tok);
        tok_size = tokend - tok;
        if (tok_size > 0) {
            arg = malloc(tok_size + 1);
            if (!arg)
                abort();
            memcpy(arg, tok, tok_size);
            arg[tok_size] = 0;
            arglist = realloc(arglist, (num_args + 2) * sizeof(char *));
            if (!arglist)
                abort();
            arglist[num_args] = arg;
            arglist[num_args + 1] = NULL;
            num_args++;
        }
        tok = tokend + 1;
    }
    free(buffer);
    return arglist;
}

void free_args(char **args)
{
    size_t i;
    if (!args)
        return;
    for (i = 0; args[i]; i++) {
        free(args[i]);
    }
    free(args);
}

int main(int argc, char **argv)
{
    pid_t child;
    while (1) {
        char **cmd = read_command();
        if (!cmd)
            continue;
        child = fork();
        if (child == 0) {
            char *const env[] = {NULL};
            if (execve(cmd[0], cmd, env) < 0) {
                perror("no exec");
                abort();
            }
        } else if (child > 0) {
            free_args(cmd);
            int status;
            pid_t done = wait(&status);
            printf("Exited: %d\n", status);
        } else if (child < 0) {
            perror("no fork");
            exit(1);
        }
    }
    return 0;
}
