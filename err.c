#if defined(__MINGW32__) || defined(__sgi)
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "err.h"
#include "progname.h"

void verr(int eval, const char *fmt, va_list args)
{
	vwarn(fmt, args);
	exit(eval);
}

void verrx(int eval, const char *fmt, va_list args)
{
	vwarnx(fmt, args);
	exit(eval);
}

void vwarn(const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", __progname);
	if (fmt) {
		vfprintf(stderr, fmt, args);
		fprintf(stderr, ": ");
	}
	fprintf(stderr, "%s\n", strerror(errno));
}

void vwarnx(const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", __progname);
	if (fmt) {
		vfprintf(stderr, fmt, args);
	}
	fprintf(stderr, "\n");
}

void err(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verr(eval, fmt, ap);
	va_end(ap);
}

void errx(int eval, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verrx(eval, fmt, ap);
	va_end(ap);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
}

void warnx(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

#endif
