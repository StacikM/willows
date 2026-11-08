#pragma once
#include <stdarg.h>
#include <stddef.h>
typedef struct _FILE FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
int printf(const char *, ...);
int vprintf(const char *, va_list);
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int fprintf(FILE *, const char *, ...);
int puts(const char *);
int putchar(int);
int getchar(void);
int fputs(const char *, FILE *);
int fputc(int, FILE *);
int fflush(FILE *);
char *fgets(char *, int, FILE *);
int scanf(const char *, ...);   /* supports %d %u %s %c only */
#define putc fputc
