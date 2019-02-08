#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

int main(int argc, char *argv[]) {
	struct passwd *puid;
	uid_t uid = geteuid();
	(void)argc; (void)argv;

	puid = getpwuid(uid);
	if (puid == NULL) {
		fprintf(stderr, "whoami: cannot find name for user ID %d\n", (int)uid);
		return 1;
	}

	printf("%s\n", puid->pw_name);
	return 0;
}
