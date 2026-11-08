#pragma once
#include <stddef.h>
typedef long ssize_t;
ssize_t readlink(const char *, char *, size_t);
