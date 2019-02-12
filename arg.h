#ifndef ARG_H
#define ARG_H

#include <stdlib.h>
#include <string.h>

#define ARG_WORD		(-1)
#define ARG_UNKNOWN_LONG	(-2)

struct long_def {
	int value;
	const char *long_arg;
};

struct arg_state {
	const struct long_def *defs;
	int argc;
	char **argv;
	int short_arg;
	int end;
};

void start_args(struct arg_state *state, const struct long_def *defs,
		int argc, char **argv, int skip);

// -abcd returns 'a' then 'b' then 'c' then 'd' in order
// --longarg returns long_def.value for the matching def
// returns 0 when all arguments have been parsed
// stops parsing -a and --longargs when -- is reached or end_of_opts is called
// if an argument is not recognized as an option, ARG_WORD is returned and
// *value set to the argument.
int next_arg(struct arg_state *state, const char **value);

void end_of_options(struct arg_state *state);

#endif
