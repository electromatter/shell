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

#include "arg.h"

static const struct long_def long_args[] = {
    {'b', "--number-nonblank"},
    {'E', "--show-ends"},
    {'n', "--number"},
    {'s', "--squeeze-blank"},
    {0, NULL},
};

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

    fwrite(buf, 1, size, stdout);
}

void do_cat(const char *name, struct options *opts)
{
    static char buf[1024 * 1024];
    FILE *file;

    if (!strcmp(name, "-")) {
        file = stdin;
    } else {
        file = fopen(name, "rb");
        if (file == NULL) {
            fprintf(stderr, "Could not open %s: %s", name, strerror(errno));
            return;
        }
    }

    while (fgets(buf, sizeof(buf), file))
        write_output(buf, strlen(buf), opts);

    if (file != stdin)
        fclose(file);
}

int main(int argc, char **argv)
{
    int c, has_files;
    const char *arg;
    struct arg_state arg_state;
    struct options o;
    memset(&o, 0, sizeof(o));

    has_files = 0;
    start_args(&arg_state, long_args, argc, argv, 1);
    while ((c = next_arg(&arg_state, &arg))) {
        switch (c) {
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
        case ARG_WORD:
            do_cat(arg, &o);
            has_files = 1;
            break;
	case ARG_UNKNOWN_LONG:
            fprintf(stderr, "cat: unknown argument '%s'\n", arg);
            return 1;
        default:
            fprintf(stderr, "cat: unknown argument '%c'\n", (char)c);
            return 1;
        }
    }

    if (!has_files)
        do_cat("-", &o);

    return 0;
}
