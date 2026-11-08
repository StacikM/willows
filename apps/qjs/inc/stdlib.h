#pragma once
#include <stddef.h>
void *malloc(size_t); void *calloc(size_t, size_t); void *realloc(void *, size_t); void free(void *);
void abort(void); void exit(int); int atoi(const char *); double strtod(const char *, char **); long strtol(const char *, char **, int);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#include <alloca.h>
int abs(int); long labs(long);
