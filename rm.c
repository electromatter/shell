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

char *avsprintf(const char *fmt, va_list args)
{
	va_list temp;
	int real_size;
	char *buf;

	va_copy(temp, args);
	real_size = vsnprintf(NULL, 0, fmt, temp);
	va_end(temp);

	if (real_size < 0 || real_size == INT_MAX)
		return NULL;

	buf = malloc(real_size + 1);
	if (buf == NULL)
		return NULL;

	if (vsnprintf(buf, real_size, fmt, args) != real_size) {
		free(buf);
		return NULL;
	}

	return buf;
}

char *asprintf(const char *fmt, ...)
{
	va_list args;
	char *ret;
	va_start(args, fmt);
	ret = avsprintf(fmt, args);
	va_end(args);
	return ret;
}

int do_remove(const char *name, int recursive, int force)
{
	int i = unlink(name);

	if (i < 0 && recursive == 1){
		DIR *dir = opendir(name);
		if (!dir)
			return;
		while (1)
		{
			struct dirent *read_dir = readdir(dir);
			if (read_dir == NULL)
			{
				break;
			}
			else{
				char *child = asprintf("%s/%s", name, read_dir->d_name);
				remove(child);
				free(child);
			}
		}
		closedir(dir);
		rmdir(name);
	}
	else if (i < 0){
		fprintf(stderr, "rm: cannot remove '%s': %s\n", name, strerror(errno));
	}
}

int main(int argc, char *argv[])
{
	do_remove(argv[1], 1, 0);
}
