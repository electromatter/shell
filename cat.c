#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

struct arg {
    char single;
    const char *argument;
};

static const struct arg args[] = {
    {'b', "--number-nonblank"},
    {'E', "--show-ends"},
    {'n', "--number"},
    {'s', "--squeeze-blank"},
    {'-', "--"},
    {0, NULL},
};

int match_arg(const char *arg)
{
    char short_arg[3] = "-*";
    const struct arg *cur;
    for (cur = &args[0]; cur->single; cur++) {
        short_arg[1] = cur->single;
        if (!strcmp(arg, short_arg) || !strcmp(arg, cur->argument))
            return cur->single;
    }
    return 0;
}

struct options {
    int number_non_empty;
    int number;
    int squeeze;
    int show_end;

    size_t count;
    int within_line;
    int last_was_lf;
};

void do_squeeze(char *buf, size_t *size, struct options *opts)
{
    char *out = buf, *end = buf + *size, *input = buf;
    if (!opts->squeeze)
        return;
    for (; input < end; input++) {
        if (*input == '\n') {
            if (opts->last_was_lf < 2) {
                *out++ = *input;
                opts->last_was_lf++;
            }
        } else {
            *out++ = *input;
            opts->last_was_lf = 0;
        }
    }
    *size = out - buf;
}

int print_numbers_and_ends(char *buf, size_t size, struct options *opts)
{
    char *ptr = buf, *line_end, *end = buf + size;

    if (!opts->number && !opts->number_non_empty && !opts->show_end)
        return 0;

    while (ptr < end) {
        if (*ptr == '\n') {
            if (!opts->within_line && opts->number)
                fprintf(stdout, "%6zd  ", ++opts->count);
            if (opts->show_end)
                putc('$', stdout);
            putc('\n', stdout);
            opts->within_line = 0;
            ptr++;
        } else {
            if (!opts->within_line && (opts->number || opts->number_non_empty))
                fprintf(stdout, "%6zd  ", ++opts->count);
            opts->within_line = 1;
            line_end = memchr(ptr, '\n', size);
            if (!line_end) {
                fwrite(ptr, 1, end - ptr, stdout);
                ptr = end;
                continue;
            }
            fwrite(ptr, 1, line_end - ptr, stdout);
            ptr = line_end;
        }
    }

    return 1;
}

void write_output(char *buf, size_t size, struct options *opts)
{
    do_squeeze(buf, &size, opts);

    if (print_numbers_and_ends(buf, size, opts))
        return;

    write(STDOUT_FILENO, buf, size);
}

void do_cat(const char *name, struct options *opts)
{
    int fd;
    char buf[10];
    ssize_t ret;

    if (!strcmp(name, "-")) {
        fd = STDIN_FILENO;
    } else {
        fd = open(name, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "Could not open %s: %s", name, strerror(errno));
            return;
        }
    }

    while (1) {
        ret = read(fd, buf, sizeof(buf));
        if (ret <= 0)
            break;
        write_output(buf, ret, opts);
    }

    if (fd != STDIN_FILENO)
        close(fd);
}

int main(int argc, char **argv)
{
    int i, arg;
    struct options o;
    memset(&o, 0, sizeof(o));
    for (i = 1; i < argc && (arg = match_arg(argv[i])); i++) {
        switch (arg) {
        case 'b':
            o.number_non_empty = 1;
            o.number = 0;
            break;
        case 'E':
            o.show_end = 1;
            break;
        case 'n':
            if (!o.number_non_empty)
                o.number = 1;
            break;
        case 's':
            o.squeeze = 1;
            break;
        case '-':
            goto files;
            break;
        default:
            abort();
        }
    }

files:

    if (i == argc) {
        do_cat("-", &o);
    }

    for (; i < argc; i++) {
        do_cat(argv[i], &o);
    }

    return 0;
}
