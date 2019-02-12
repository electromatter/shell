#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>

#include "arg.h"

int do_rmdir(const char *name)
{
	if (rmdir(name) < 0) {
		fprintf(stderr, "rmdir: failed to remove '%s': %s\n", name, strerror(errno));
		return 1;
	}
	return 0;
}

int do_rmdir_parents(char *name)
{
	char *end = name + strlen(name);
	int ret = 0, saved;

	while (name < end) {
		while (name < end && end[-1] == '/')
			end--;

		saved = *end;
		*end = 0;
		ret |= do_rmdir(name);
		if (ret)
			break;
		*end = saved;

		while (name < end && end[-1] != '/')
			end--;
	}

	return ret;
}

static const struct long_def long_args[] = {
	{'p', "--parents"},
	{0, NULL},
};

int main(int argc, char *argv[])
{
	struct arg_state arg_state;
	const char *arg;
	int c;
	int ret = 0;
	int parents = 0;

	start_args(&arg_state, long_args, argc, argv, 1);
	while ((c = next_arg(&arg_state, &arg))) {
		switch (c) {
		case 'p':
			parents = 1;
			break;
		case ARG_WORD:
			if (parents)
				ret |= do_rmdir_parents((char *)arg);
			else
				ret |= do_rmdir(arg);
			break;
		case ARG_UNKNOWN_LONG:
			fprintf(stderr, "ps: unknown argument: '%s'\n", arg);
			return 1;
		default:
			fprintf(stderr, "ps: unknown argument: '%c'\n", (char)c);
			return 1;
		}
	}

	return ret;
}
