#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "arg.h"

static int is_dir(const char *name) {
	struct stat statbuf;
	if (stat(name, &statbuf) < 0)
		return -1;
	return (statbuf.st_mode & S_IFMT) == S_IFDIR;
}

static int do_mkdir(const char *name, int fail_if_exists, int verbose)
{
	int ret = mkdir(name, 0777);

	if (ret < 0) {
		if (errno == EEXIST && !fail_if_exists) {
			if (is_dir(name) > 0)
				return 0;
			errno = EEXIST;
		}

		fprintf(stderr, "mkdir: cannot create directory '%s': %s\n",
			name, strerror(errno));
		return 1;
	}

	if (verbose)
		printf("mkdir: created directory '%s'\n", name);

	return 0;
}

static int do_mkdir_parents(char *name, int verbose)
{
	size_t i, end;
	int err = 0;

	end = strlen(name);
	for (i = 0; i < end; i++) {
		while (name[i] != '/' && i < end)
			i++;
		name[i] = 0;
		if (i > 0)
			err |= do_mkdir(name, 0, verbose);
		name[i] = '/';
	}

	return err;
}

static const struct arg_def mkdir_args[] = {
	{'v', "--verbose"},
	{'p', "--parents"},
	{'-', "--"},
	{0, NULL}
};

int main(int argc, char **argv)
{
	int parents = 0;
	int verbose = 0;
	int err = 0;
	int i;

	for (i = 1; i < argc; i++) {
		switch (match_arg(mkdir_args, argv[i])) {
		case 'v':
			verbose = 1;
			break;
		case 'p':
			parents = 1;
			break;
		case '-':
			i++;
			goto args_done;
		default:
			goto args_done;
		}
	}

args_done:

	for (; i < argc; i++) {
		if (parents) {
			err |= do_mkdir_parents(argv[i], verbose);
		} else {
			err |= do_mkdir(argv[i], 1, verbose);
		}
	}

	return err;
}
