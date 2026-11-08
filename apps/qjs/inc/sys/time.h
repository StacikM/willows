#pragma once
#include <time.h>
struct timeval { long long tv_sec; long long tv_usec; };
struct timezone { int tz_minuteswest, tz_dsttime; };
int gettimeofday(struct timeval *, struct timezone *);
