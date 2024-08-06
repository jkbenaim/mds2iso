#pragma once

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(__MINGW32__) || defined(__sgi)

void err(int eval, const char *fmt, ...);
void errx(int eval, const char *fmt, ...);
void warn(const char *fmt, ...);
void warnx(const char *fmt, ...);

void verr(int eval, const char *fmt, va_list args);
void verrx(int eval, const char *fmt, va_list args);
void vwarn(const char *fmt, va_list args);
void vwarnx(const char *fmt, va_list args);

#else
#include <err.h>
#endif
