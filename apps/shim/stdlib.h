#pragma once
#include <stddef.h>
void *malloc(size_t); void *calloc(size_t, size_t); void *realloc(void *, size_t); void free(void *);
void exit(int); void abort(void); int atexit(void (*)(void));
int atoi(const char *); int abs(int); int rand(void); void srand(unsigned);
#define RAND_MAX 32767
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
