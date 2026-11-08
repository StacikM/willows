#pragma once
/* Willows runs the JS engine on a single thread: the pthread API is a set of no-op stubs. */
#include <time.h>
#include <errno.h>
typedef int pthread_once_t; typedef int pthread_mutex_t; typedef int pthread_cond_t; typedef int pthread_condattr_t; typedef int pthread_mutexattr_t;
typedef int pthread_attr_t; typedef unsigned long pthread_t;
#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_COND_INITIALIZER 0
#define PTHREAD_CREATE_DETACHED 1
static inline int pthread_once(pthread_once_t *o, void (*f)(void)){ if(!*o){ *o = 1; f(); } return 0; }
static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a){ (void)m; (void)a; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m){ (void)m; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m){ (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m){ (void)m; return 0; }
static inline int pthread_condattr_init(pthread_condattr_t *a){ (void)a; return 0; }
static inline int pthread_condattr_destroy(pthread_condattr_t *a){ (void)a; return 0; }
static inline int pthread_condattr_setclock(pthread_condattr_t *a, int c){ (void)a; (void)c; return 0; }
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a){ (void)c; (void)a; return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c){ (void)c; return 0; }
static inline int pthread_cond_signal(pthread_cond_t *c){ (void)c; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c){ (void)c; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m){ (void)c; (void)m; return 0; }
static inline int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t){ (void)c; (void)m; (void)t; return ETIMEDOUT; }
static inline int pthread_attr_init(pthread_attr_t *a){ (void)a; return 0; }
static inline int pthread_attr_destroy(pthread_attr_t *a){ (void)a; return 0; }
static inline int pthread_attr_setdetachstate(pthread_attr_t *a, int s){ (void)a; (void)s; return 0; }
static inline int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *arg){ (void)t; (void)a; (void)f; (void)arg; return EAGAIN; }
static inline int pthread_join(pthread_t t, void **r){ (void)t; (void)r; return 0; }
static inline pthread_t pthread_self(void){ return 1; }
static inline int pthread_attr_setstacksize(pthread_attr_t *a, unsigned long s){ (void)a; (void)s; return 0; }
