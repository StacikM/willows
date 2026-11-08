#pragma once
#include <stdarg.h>
#include <stddef.h>
typedef struct _FILE FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define BUFSIZ 512
int printf(const char *, ...); int vprintf(const char *, va_list);
int snprintf(char *, size_t, const char *, ...); int vsnprintf(char *, size_t, const char *, va_list);
int sprintf(char *, const char *, ...);
int fprintf(FILE *, const char *, ...); int vfprintf(FILE *, const char *, va_list);
int puts(const char *); int putchar(int); int fputc(int, FILE *); int fputs(const char *, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *); int fflush(FILE *);
#define putc fputc
