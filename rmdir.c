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

static const struct arg_def args[] = {
	{'p', "--parents"},
	{'-', "--"},
	{0, NULL},
};

int main(int argc, char *argv[])
{
	int ret = 0;
	int parents = 0;
	int i;
	for (i = 1; i < argc; i++) {
		switch (match_arg(args, argv[i])) {
		case 'p':
			parents = 1;
			break;
		default:
			goto end_args;
		}
	}
end_args:
	for (; i < argc; i++) {
		if (parents)
			ret |= do_rmdir_parents(argv[i]);
		else
			ret |= do_rmdir(argv[i]);
	}
	return ret;
}
