#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "aprintf.h"

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

int exists(const char *name)
{
    struct stat sb;
    return stat(name, &sb) == 0;
}

char *slice(const char *str, size_t len)
{
    char *buf = malloc(len + 1), *ptr;
    if (!buf)
        return NULL;
    strncpy(buf, str, len);
    buf[len] = 0;
    ptr = realloc(buf, strlen(buf) + 1);
    return !ptr ? buf : ptr;
}

char *find_on_path(const char *cmd)
{
    const char *path, *end;
    char *name, *prefix;
    size_t prefix_len;

    if (strchr(cmd, '/'))
        return strdup(cmd);

    path = getenv("PATH");
    while (path) {
        end = strchr(path, ':');
        if (!end) {
            prefix_len = strlen(path);
        } else {
            prefix_len = end - path;
        }

        if (!prefix_len) {
            prefix = strdup(".");
        } else {
            prefix = slice(path, prefix_len);
        }

        if (!prefix)
            return NULL;

        name = asprintf("%s/%s", prefix, cmd);

        free(prefix);

        if (name && exists(name))
            return name;

        free(name);
        path = end ? end + 1 : NULL;
    }
    return NULL;
}

extern char **environ;

int main(int argc, char **argv)
{
    pid_t child;
    while (1) {
        char **cmd = read_command();
        char *path = cmd ? find_on_path(cmd[0]) : NULL;
        if (!path || !cmd) {
            free_args(cmd);
            free(path);
            continue;
        }
        child = fork();
        if (child == 0) {
            if (execve(path, cmd, environ) < 0) {
                perror("no exec");
                abort();
            }
        } else if (child > 0) {
            free_args(cmd);
            free(path);
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
