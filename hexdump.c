#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

size_t do_hexdump(const unsigned char *buf, size_t size, size_t offset)
{
	size_t i;

	assert(size <= 16);
	printf("%08zx  ", offset);
	for (i = 0; i < 16; i++) {
		if (i < size) {
			printf("%02x ", buf[i]);
		} else {
			printf("   ");
		}

		if (i == 7)
			printf(" ");
	}

	printf(" |");
	for (i = 0; i < size; i++) {
		if (buf[i] >= 32 && buf[i] < 127) {
			printf("%c", buf[i]);
		} else {
			printf(".");
		}
	}
	printf("|\n");

	return offset + size;
}

int main(int argc, char *argv[])
{
	char *default_argv[] = {argv[0], "-"};
	char buf[16];
	size_t size = 0;
	size_t offset = 0;
	FILE *file = NULL;
	int i = 1;

	if (argc < 2) {
		argv = default_argv;
		argc = 2;
	}

	while (1) {
		if (file == NULL || feof(file)) {
			if (file != NULL && file != stdin)
				fclose(file);

			if (i >= argc)
				break;

			if (!strcmp(argv[i], "-")) {
				file = stdin;
			} else {
				file = fopen(argv[i], "rb");
				if (file == NULL) {
					fprintf(stderr, "hexdump: %s: %s\n",
						argv[i], strerror(errno));
				}
			}

			i++;

			continue;
		}

		if (size < sizeof(buf)) {
			size += fread(buf + size, 1, sizeof(buf) - size, file);
			continue;
		}

		offset = do_hexdump((void *)buf, size, offset);
		size = 0;
	}

	if (size > 0)
		offset = do_hexdump((void *)buf, size, offset);

	if (offset > 0)
		printf("%08zx\n", offset);

	return 0;
}

