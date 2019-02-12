#include <assert.h>

#include "arg.h"

void start_args(struct arg_state *state, const struct long_def *defs,
		int argc, char **argv, int skip)
{
	if (skip < 0)
		skip = 0;
	assert(argc >= 0);
	if (argc < 0)
		argc = 0;
	state->defs = defs;
	state->argc = argc - skip;
	state->argv = argv + skip;
	state->short_arg = 0;
	state->end = 0;
}

// -abcd returns 'a' then 'b' then 'c' then 'd' in order
// --longarg returns long_def.value for the matching def
// returns 0 when all arguments have been parsed
// stops parsing -a and --longargs when -- is reached or end_of_opts is called
// if an argument is not recognized as an option, ARG_WORD is returned and
// *value set to the argument.
int next_arg(struct arg_state *state, const char **value)
{
	const char *arg;
	const struct long_def *def;

	if (value)
		*value = NULL;

	while (state->argc > 0) {
		arg = *state->argv;
		if (state->short_arg) {
			if (arg[state->short_arg])
				return arg[state->short_arg++];
			state->short_arg = 0;
			state->argc--;
			state->argv++;
			continue;
		}

		if (!strcmp(arg, "--")) {
			state->end = 1;
		}

		if (!state->end) {
			if (!strncmp(arg, "--", 2)) {
				for (def = state->defs; def && def->long_arg; def++) {
					if (!strcmp(arg, def->long_arg)) {
						state->argc--;
						state->argv++;
						return def->value;
					}
				}
				if (value)
					*value = arg;
				return ARG_UNKNOWN_LONG;
			} else if (*arg == '-' && arg[1]) {
				state->short_arg = 1;
				return arg[state->short_arg++];
			}
		}

		if (value)
			*value = arg;
		state->argc--;
		state->argv++;
		return ARG_WORD;
	}

	return 0;
}

void end_of_options(struct arg_state *state)
{
	state->end = 1;
}
