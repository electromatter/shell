#ifndef ARG_H
#define ARG_H

#include <string.h>

struct arg_def {
	char single;
	const char *long_arg;
};

static inline int match_arg(const struct arg_def *defs, const char *arg)
{
	char short_arg[3] = "-*";
	for (; defs->single; defs++) {
		short_arg[1] = defs->single;
		if (!strcmp(arg, short_arg))
			return defs->single;
		if (defs->long_arg && !strcmp(arg, defs->long_arg))
			return defs->single;
	}
	return 0;
}

#endif
