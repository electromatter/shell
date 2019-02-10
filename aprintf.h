#ifndef APRINTF_H
#define APRINTF_H

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>

static inline char *avsprintf(const char *fmt, va_list args)
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

static inline char *asprintf(const char *fmt, ...)
{
	va_list args;
	char *ret;
	va_start(args, fmt);
	ret = avsprintf(fmt, args);
	va_end(args);
	return ret;
}

#endif
