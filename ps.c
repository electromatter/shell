#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>

#include "arg.h"

void show_process(pid_t pid, int show_all)
{
	char name[64];
	struct stat sb;
	struct passwd *puid;
	FILE *s;

	sprintf(name, "/proc/%d", pid);
	if (stat(name, &sb) < 0)
		return;

	if (!show_all && sb.st_uid != geteuid())
		return;

	puid = getpwuid(sb.st_uid);
	if (puid)
		printf("%s\t", puid->pw_name);
	else
		printf("#%d\t", (int)sb.st_uid);

	printf("%d\t", (int)pid);

	sprintf(name, "/proc/%d/comm", (int)pid);
	s = fopen(name, "rb");
	if (s) {
		fscanf(s, "%63s\n", name);
		printf("%s\t", name);
		fclose(s);
	} else {
		printf("???\t");
	}
	printf("\n");
}

int main(int argc, char *argv[])
{
	struct arg_state arg_state;
	const char *arg;
	DIR *dir;
	struct dirent *ent;
	int pid;
	int show_all = 0;
	int c;

	start_args(&arg_state, NULL, argc, argv, 1);
	while ((c = next_arg(&arg_state, &arg))) {
		switch (c) {
		case 'A':
			show_all = 1;
			break;
		case ARG_WORD:
		case ARG_UNKNOWN_LONG:
			fprintf(stderr, "ps: unknown argument: '%s'\n", arg);
			return 1;
		default:
			fprintf(stderr, "ps: unknown argument: '%c'\n", (char)c);
			return 1;
		}
	}

	dir = opendir("/proc");
	if (!dir) {
		perror("Unable to open proc");
		return 1;
	}

	printf("USER\tPID\tCOMMAND\n");
	while ((ent = readdir(dir)) != NULL) {
		if (sscanf(ent->d_name, "%d", &pid) != 1)
			continue;
		show_process(pid, show_all);
	}

	return 0;
}
